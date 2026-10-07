/*
 * Unit tests for port/dns-sd_mdns.c
 *
 * Covers:
 *   knx_dns_sd_update_service   - one announcement at once, a repeat one second
 *                                 later (RFC 6762 section 8.3), a restart of the
 *                                 repeat on a change within that second
 *                                 (RFC 6762 section 8.4), NULL serial number,
 *                                 rollback of the record on a send failure
 *   knx_dns_sd_set_sleep_period - SP in the TXT record, the TXT goodbye when SP
 *                                 goes back to 0, the same repeat as above
 *   knx_dns_sd_stop             - the goodbye cancels a pending repeat
 *   knx_dns_sd_get_used_port    - the port of the IP context
 *   knx_dns_sd_get_current_record - the snapshot before, after and past a publish
 *   mdns_query_callback (static, through the listener thread) - PTR, subtype
 *                                 PTR, SRV, AAAA, TXT and ANY queries, and the
 *                                 queries the responder ignores
 *
 * Requirements:
 *   oc_mmem_init() and oc_ri_init() - the repeat is a delayed callback on the
 *   RI timed-event list, and oc_main_poll() drives that list.
 *
 * Four symbols are redirected with the GNU linker --wrap facility, see
 * tests/unit/CMakeLists.txt:
 *   sendto                    - records every packet, nothing leaves the host
 *   recvmsg                   - hands the listener thread the queries the test
 *                               queued, as received on loopback (IPV6_PKTINFO),
 *                               and an empty socket otherwise, so no real query
 *                               of the host network reaches it
 *   get_ip_context_for_device - one fake unicast IPv6 endpoint on loopback, so
 *                               collect_if_indices() finds an interface without
 *                               oc_connectivity_init()
 *   oc_clock_time             - a test-controlled clock, so the one-second
 *                               delay needs no real sleep
 *
 * The query responder runs on the real listener thread of dns-sd_mdns.c, which
 * polls its socket every 100 ms. The responder tests therefore wait for the
 * answer with a bounded poll (3 s), and the negative cases wait 300 ms for an
 * answer that never comes.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

extern "C" {
#include "oc_api.h"          /* oc_main_poll */
#include "oc_core_res.h"     /* oc_core_set_device_hostname */
#include "oc_endpoint.h"     /* oc_endpoint_t, IPV6 */
#include "oc_ri.h"           /* oc_ri_init / oc_ri_shutdown */
#include "port/dns-sd.h"     /* functions under test */
#include "port/oc_clock.h"   /* OC_CLOCK_SECOND */
#include "ipadapter.h"       /* ip_context_t */
#include "util/oc_list.h"
#include "util/oc_mmem.h"
}

/* -- DNS constants ---------------------------------------------------------- */

static const uint16_t DNS_TYPE_PTR = 12;
static const uint16_t DNS_TYPE_TXT = 16;
static const uint16_t DNS_TYPE_AAAA = 28;
static const uint16_t DNS_TYPE_SRV = 33;
static const uint16_t DNS_TYPE_ANY = 255;
static const uint16_t DNS_CLASS_IN = 1;
static const uint16_t MDNS_PORT = 5353;

static const char *const SERIAL = "00fa10020800";
/* the default host name label the stack derives from SERIAL, mDNS announces it with '.local' appended */
static const char *const HOSTNAME = "knx-00fa10020800";
static const uint64_t IID = 0x1199887766;
static const uint16_t IA = 0x1101;
static const uint16_t COAP_PORT = 5683;

/* -- captured packets ------------------------------------------------------ */

struct CapturedPacket {
  std::vector<uint8_t> bytes;
  std::string first_name;  /* owner name of the first resource record */
  uint16_t first_type;
  uint32_t first_ttl;
};

static const uint32_t TTL_UNKNOWN = UINT32_MAX;

static std::mutex g_lock;
static std::vector<CapturedPacket> g_packets;
static std::vector<std::vector<uint8_t>> g_queries;
static std::atomic<bool> g_sendto_fails{false};

/*
  Decode the first resource record head of a DNS message: owner name, type and
  TTL. A goodbye carries TTL 0 (RFC 6762 section 10.1), an announcement or an
  answer carries TTL > 0.
*/
static void parse_first_record(CapturedPacket &p)
{
  const std::vector<uint8_t> &buf = p.bytes;
  const size_t len = buf.size();
  const size_t header_len = 12;
  const size_t question_tail = 4;  /* qtype + qclass */
  const size_t record_tail = 8;    /* type + class + ttl */

  p.first_name.clear();
  p.first_type = 0;
  p.first_ttl = TTL_UNKNOWN;

  if (len < header_len) {
    return;
  }
  const uint16_t qdcount = (uint16_t)((buf[4] << 8) | buf[5]);
  size_t off = header_len;

  auto skip_name = [&](std::string *out) -> bool {
    while (off < len) {
      const uint8_t label = buf[off];
      if (label == 0) {
        off += 1;
        return true;
      }
      if ((label & 0xC0) == 0xC0) {
        off += 2;
        return true;
      }
      if (out) {
        if (!out->empty()) {
          out->push_back('.');
        }
        out->append((const char *)&buf[off + 1], label);
      }
      off += 1 + label;
    }
    return false;
  };

  for (uint16_t q = 0; q < qdcount; q++) {
    if (!skip_name(nullptr)) {
      return;
    }
    off += question_tail;
  }
  if (!skip_name(&p.first_name) || off + record_tail > len) {
    p.first_name.clear();
    return;
  }
  p.first_type = (uint16_t)((buf[off] << 8) | buf[off + 1]);
  p.first_ttl = ((uint32_t)buf[off + 4] << 24) | ((uint32_t)buf[off + 5] << 16) |
                ((uint32_t)buf[off + 6] << 8) | (uint32_t)buf[off + 7];
}

static std::vector<CapturedPacket> packets(void)
{
  std::lock_guard<std::mutex> guard(g_lock);
  return g_packets;
}

static size_t packet_count(void)
{
  std::lock_guard<std::mutex> guard(g_lock);
  return g_packets.size();
}

static CapturedPacket packet_at(size_t index)
{
  std::lock_guard<std::mutex> guard(g_lock);
  return g_packets.at(index);
}

static size_t count_announcements(void)
{
  size_t n = 0;
  for (const auto &p : packets()) {
    if (p.first_ttl != 0 && p.first_ttl != TTL_UNKNOWN) {
      n++;
    }
  }
  return n;
}

static size_t count_goodbyes(void)
{
  size_t n = 0;
  for (const auto &p : packets()) {
    if (p.first_ttl == 0) {
      n++;
    }
  }
  return n;
}

static std::string hex(const std::vector<uint8_t> &bytes)
{
  std::string out;
  char item[4];
  for (uint8_t b : bytes) {
    snprintf(item, sizeof(item), "%02x ", b);
    out += item;
  }
  return out;
}

static bool contains(const std::vector<uint8_t> &bytes, const char *text)
{
  const size_t n = strlen(text);
  if (bytes.size() < n) {
    return false;
  }
  for (size_t i = 0; i + n <= bytes.size(); i++) {
    if (memcmp(&bytes[i], text, n) == 0) {
      return true;
    }
  }
  return false;
}

/* -- query builder ---------------------------------------------------------- */

static std::vector<uint8_t> make_query(const char *name, uint16_t qtype)
{
  std::vector<uint8_t> q = {0x12, 0x34, 0x00, 0x00, 0x00, 0x01,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  std::string rest(name);
  if (!rest.empty() && rest.back() == '.') {
    rest.pop_back();
  }
  while (!rest.empty()) {
    const size_t dot = rest.find('.');
    const std::string label = rest.substr(0, dot);
    q.push_back((uint8_t)label.size());
    q.insert(q.end(), label.begin(), label.end());
    rest = (dot == std::string::npos) ? "" : rest.substr(dot + 1);
  }
  q.push_back(0);
  q.push_back((uint8_t)(qtype >> 8));
  q.push_back((uint8_t)(qtype & 0xFF));
  q.push_back((uint8_t)(DNS_CLASS_IN >> 8));
  q.push_back((uint8_t)(DNS_CLASS_IN & 0xFF));
  return q;
}

/* -- linker --wrap stubs -------------------------------------------------- */

extern "C" ssize_t __wrap_sendto(int, const void *buf, size_t len, int,
                                 const struct sockaddr *, socklen_t)
{
  CapturedPacket p;
  p.bytes.assign((const uint8_t *)buf, (const uint8_t *)buf + len);
  parse_first_record(p);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_packets.push_back(p);
  }
  if (g_sendto_fails) {
    errno = ENETUNREACH;
    return -1;
  }
  return (ssize_t)len;
}

extern "C" ssize_t __wrap_recvmsg(int, struct msghdr* msg, int)
{
  std::lock_guard<std::mutex> guard(g_lock);
  if (g_queries.empty()) {
    errno = EWOULDBLOCK;
    return -1;
  }
  const std::vector<uint8_t> q = g_queries.front();
  g_queries.erase(g_queries.begin());
  if (msg == nullptr || msg->msg_iovlen < 1 || q.size() > msg->msg_iov[0].iov_len) {
    errno = EMSGSIZE;
    return -1;
  }
  memcpy(msg->msg_iov[0].iov_base, q.data(), q.size());
  if (msg->msg_name && msg->msg_namelen >= sizeof(struct sockaddr_in6)) {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(MDNS_PORT);
    addr.sin6_addr.s6_addr[15] = 1;
    memcpy(msg->msg_name, &addr, sizeof(addr));
    msg->msg_namelen = sizeof(addr);
  }
  /* The receive interface, as the kernel delivers it with IPV6_RECVPKTINFO:
   * the query arrived on loopback, addressed to FF02::FB. */
  msg->msg_flags = 0;
  if (msg->msg_control && msg->msg_controllen >= CMSG_SPACE(sizeof(struct in6_pktinfo))) {
    struct cmsghdr* cm = CMSG_FIRSTHDR(msg);
    cm->cmsg_level = IPPROTO_IPV6;
    cm->cmsg_type = IPV6_PKTINFO;
    cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
    struct in6_pktinfo* pi = (struct in6_pktinfo*)CMSG_DATA(cm);
    memset(pi, 0, sizeof(*pi));
    pi->ipi6_addr.s6_addr[0] = 0xFF;
    pi->ipi6_addr.s6_addr[1] = 0x02;
    pi->ipi6_addr.s6_addr[15] = 0xFB;
    pi->ipi6_ifindex = if_nametoindex("lo");
    msg->msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
  } else {
    msg->msg_controllen = 0;
  }
  return (ssize_t)q.size();
}

static ip_context_t g_fake_ctx;
static oc_endpoint_t g_fake_ep;

extern "C" ip_context_t *__wrap_get_ip_context_for_device(void)
{
  return &g_fake_ctx;
}

static oc_clock_time_t g_now;

extern "C" oc_clock_time_t __wrap_oc_clock_time(void)
{
  return g_now;
}

/* -- fixture ---------------------------------------------------------------- */

class DnsSd : public ::testing::Test {
protected:
  void SetUp() override
  {
    /* start away from 0, so an expired timer never looks like an unset one */
    g_now = 100 * OC_CLOCK_SECOND;
    g_sendto_fails = false;
    oc_mmem_init();
    oc_ri_init();
    ASSERT_EQ(oc_core_set_device_hostname(HOSTNAME), 0);

    memset(&g_fake_ctx, 0, sizeof(g_fake_ctx));
    OC_LIST_STRUCT_INIT(&g_fake_ctx, eps);
    g_fake_ctx.port = COAP_PORT;

    memset(&g_fake_ep, 0, sizeof(g_fake_ep));
    g_fake_ep.flags = IPV6;
    g_fake_ep.interface_index = (int)if_nametoindex("lo");
    ASSERT_GT(g_fake_ep.interface_index, 0);
    g_fake_ep.addr.ipv6.port = COAP_PORT;
    g_fake_ep.addr.ipv6.address[15] = 1;
    oc_list_add(g_fake_ctx.eps, &g_fake_ep);

    clear_packets();
  }

  void TearDown() override
  {
    g_sendto_fails = false;
    /* the sleep period is file-static state that knx_dns_sd_stop() keeps */
    if (knx_dns_sd_get_current_record().knx.sp != 0) {
      knx_dns_sd_set_sleep_period(0);
    }
    knx_dns_sd_stop();
    oc_ri_shutdown();
    clear_packets();
  }

  static void clear_packets(void)
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_packets.clear();
    g_queries.clear();
  }

  /* move the clock and let the RI event loop run every due callback */
  static void advance_ms(unsigned ms)
  {
    g_now += (oc_clock_time_t)ms * OC_CLOCK_SECOND / 1000;
    (void)oc_main_poll();
    (void)oc_main_poll();
  }

  static int publish(bool pm)
  {
    return knx_dns_sd_update_service(SERIAL, IID, IA, pm);
  }

  static void queue_query(const char *name, uint16_t qtype)
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_queries.push_back(make_query(name, qtype));
  }

  /* the listener thread polls every 100 ms; give it a bounded time to answer */
  static bool wait_for_packets(size_t total, unsigned timeout_ms = 3000)
  {
    for (unsigned waited = 0; waited < timeout_ms; waited += 10) {
      if (packet_count() >= total) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return packet_count() >= total;
  }

  static void expect_no_more_packets(size_t total)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(packet_count(), total);
  }

  /* publish, then ask the responder one question and return its one answer */
  static bool ask(const char *name, uint16_t qtype, CapturedPacket *answer)
  {
    const size_t base = packet_count();
    queue_query(name, qtype);
    if (!wait_for_packets(base + 1)) {
      return false;
    }
    *answer = packet_at(base);
    return true;
  }
};

/* -- knx_dns_sd_update_service --------------------------------------------- */

TEST_F(DnsSd, FirstAnnouncementLeavesAtOnce)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);

  ASSERT_EQ(count_announcements(), 1u);
  EXPECT_EQ(count_goodbyes(), 0u);

  const CapturedPacket p = packet_at(0);
  EXPECT_EQ(p.first_type, DNS_TYPE_PTR);
  EXPECT_EQ(p.first_name, "_knx._udp.local");
  EXPECT_TRUE(contains(p.bytes, SERIAL));
}

TEST_F(DnsSd, SecondAnnouncementFollowsOneSecondLater)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  ASSERT_EQ(count_announcements(), 1u);

  advance_ms(999);
  EXPECT_EQ(count_announcements(), 1u) << "the repeat left before one second";

  advance_ms(1);
  ASSERT_EQ(count_announcements(), 2u) << "no repeat after one second";
  EXPECT_EQ(count_goodbyes(), 0u);

  /* the repeat carries the same records as the first announcement */
  EXPECT_EQ(packet_at(0).bytes, packet_at(1).bytes);
}

TEST_F(DnsSd, RepeatLeavesOnlyOnce)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(1000);
  ASSERT_EQ(count_announcements(), 2u);

  advance_ms(5000);
  EXPECT_EQ(count_announcements(), 2u) << "a third unsolicited announcement left";
}

TEST_F(DnsSd, ChangeWithinOneSecondRestartsTheRepeat)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(500);

  ASSERT_EQ(publish(true), 0);
  advance_ms(0);
  ASSERT_EQ(count_goodbyes(), 1u);
  ASSERT_EQ(count_announcements(), 2u);

  /* one second after the first publish: the old record gets no repeat */
  advance_ms(500);
  EXPECT_EQ(count_announcements(), 2u) << "a stale repeat of the old record left";

  /* one second after the change: the new record gets its repeat */
  advance_ms(500);
  ASSERT_EQ(count_announcements(), 3u) << "no repeat of the new record";
  EXPECT_EQ(packet_at(3).bytes, packet_at(2).bytes);
}

TEST_F(DnsSd, ChangeSendsGoodbyeForTheOldRecordFirst)
{
  ASSERT_EQ(publish(true), 0);
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);

  ASSERT_EQ(packet_count(), 3u);
  const CapturedPacket goodbye = packet_at(1);
  EXPECT_EQ(goodbye.first_ttl, 0u);
  EXPECT_EQ(goodbye.first_type, DNS_TYPE_PTR);
  EXPECT_TRUE(contains(goodbye.bytes, "_pm")) << "the goodbye lacks the old _pm subtype";

  const CapturedPacket announce = packet_at(2);
  EXPECT_GT(announce.first_ttl, 0u);
  EXPECT_FALSE(contains(announce.bytes, "_pm")) << "the new record still carries _pm";
}

TEST_F(DnsSd, NullSerialNumberIsRejected)
{
  EXPECT_EQ(knx_dns_sd_update_service(NULL, IID, IA, false), -1);
  advance_ms(0);

  EXPECT_EQ(packet_count(), 0u);
  EXPECT_FALSE(knx_dns_sd_get_current_record().valid);
}

TEST_F(DnsSd, FailedFirstAnnouncementLeavesNoRecord)
{
  g_sendto_fails = true;
  EXPECT_EQ(publish(false), -1);
  advance_ms(0);

  EXPECT_FALSE(knx_dns_sd_get_current_record().valid);

  g_sendto_fails = false;
  advance_ms(1000);
  EXPECT_EQ(count_announcements(), 1u) << "a repeat left after a failed first announcement";
}

TEST_F(DnsSd, FailedChangeRestoresThePreviousRecord)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);

  g_sendto_fails = true;
  EXPECT_EQ(publish(true), -1);
  advance_ms(0);

  const mdns_knx_record_t record = knx_dns_sd_get_current_record();
  EXPECT_TRUE(record.valid);
  EXPECT_FALSE(record.knx.pm) << "the failed change stayed in the record";

  g_sendto_fails = false;
  const size_t before = packet_count();
  advance_ms(1000);
  EXPECT_EQ(packet_count(), before) << "a repeat left after a failed change";
}

TEST_F(DnsSd, AnnouncementWithoutInterfacesFails)
{
  oc_list_remove(g_fake_ctx.eps, &g_fake_ep);

  EXPECT_EQ(publish(false), -1);
  advance_ms(1000);

  EXPECT_EQ(packet_count(), 0u);
  EXPECT_FALSE(knx_dns_sd_get_current_record().valid);
}

/* -- knx_dns_sd_set_sleep_period ------------------------------------------- */

TEST_F(DnsSd, SleepPeriodAppearsInTheTxtRecord)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  ASSERT_EQ(packet_count(), 1u);
  EXPECT_FALSE(contains(packet_at(0).bytes, "SP="));

  knx_dns_sd_set_sleep_period(30);
  advance_ms(0);
  ASSERT_EQ(packet_count(), 2u);
  const CapturedPacket p = packet_at(1);
  EXPECT_GT(p.first_ttl, 0u);
  EXPECT_TRUE(contains(p.bytes, "SP=30"));
  EXPECT_EQ(knx_dns_sd_get_current_record().knx.sp, 30);
}

TEST_F(DnsSd, SleepPeriodChangeGetsARepeat)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(1000);
  ASSERT_EQ(count_announcements(), 2u);

  knx_dns_sd_set_sleep_period(30);
  advance_ms(0);
  ASSERT_EQ(count_announcements(), 3u);

  advance_ms(1000);
  ASSERT_EQ(count_announcements(), 4u) << "no repeat after the TXT record changed";
  EXPECT_EQ(packet_at(3).bytes, packet_at(2).bytes);
}

/* RFC 6762 section 10.1: a cleared SP needs a TTL 0 for the old TXT record. */
TEST_F(DnsSd, ClearingTheSleepPeriodSendsATxtGoodbye)
{
  ASSERT_EQ(publish(false), 0);
  knx_dns_sd_set_sleep_period(30);
  advance_ms(0);
  const size_t base = packet_count();

  knx_dns_sd_set_sleep_period(0);
  advance_ms(0);
  ASSERT_EQ(packet_count(), base + 2u);

  const CapturedPacket goodbye = packet_at(base);
  EXPECT_EQ(goodbye.first_type, DNS_TYPE_TXT) << hex(goodbye.bytes);
  EXPECT_GT(goodbye.bytes.size(), 12u) << "the goodbye carries no record";
  EXPECT_EQ(goodbye.first_ttl, 0u);
  EXPECT_EQ(goodbye.first_name, std::string(SERIAL) + "._knx._udp.local");
  EXPECT_TRUE(contains(goodbye.bytes, "SP=30"));

  const CapturedPacket announce = packet_at(base + 1);
  EXPECT_EQ(announce.first_type, DNS_TYPE_PTR);
  EXPECT_GT(announce.first_ttl, 0u);
  EXPECT_FALSE(contains(announce.bytes, "SP=")) << "the new record still carries SP";
  EXPECT_EQ(knx_dns_sd_get_current_record().knx.sp, 0);

  advance_ms(1000);
  EXPECT_EQ(packet_count(), base + 3u) << "no repeat after SP was cleared";
}

TEST_F(DnsSd, ClearingAnUnsetSleepPeriodSendsNoGoodbye)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  const size_t base = packet_count();

  knx_dns_sd_set_sleep_period(0);
  advance_ms(0);
  ASSERT_EQ(packet_count(), base + 1u);
  EXPECT_GT(packet_at(base).first_ttl, 0u);
  EXPECT_EQ(count_goodbyes(), 0u);
}

TEST_F(DnsSd, SleepPeriodBeforeAPublishSendsNothing)
{
  knx_dns_sd_set_sleep_period(30);
  advance_ms(1000);

  EXPECT_EQ(packet_count(), 0u);
  EXPECT_EQ(knx_dns_sd_get_current_record().knx.sp, 30);
}

/* -- knx_dns_sd_stop ------------------------------------------------------- */

TEST_F(DnsSd, StopSendsTheGoodbyeAndCancelsThePendingRepeat)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  knx_dns_sd_stop();
  advance_ms(0);
  ASSERT_EQ(count_announcements(), 1u);
  ASSERT_EQ(count_goodbyes(), 1u);
  EXPECT_FALSE(knx_dns_sd_get_current_record().valid);

  advance_ms(1000);
  EXPECT_EQ(count_announcements(), 1u) << "a repeat left after the goodbye";
  EXPECT_EQ(count_goodbyes(), 1u);
}

TEST_F(DnsSd, StopWithoutAPublishSendsNothing)
{
  knx_dns_sd_stop();
  advance_ms(1000);

  EXPECT_EQ(packet_count(), 0u);
}

/* -- knx_dns_sd_get_used_port / knx_dns_sd_get_current_record -------------- */

TEST_F(DnsSd, UsedPortComesFromTheIpContext)
{
  EXPECT_EQ(knx_dns_sd_get_used_port(), COAP_PORT);
}

TEST_F(DnsSd, CurrentRecordFollowsThePublish)
{
  EXPECT_FALSE(knx_dns_sd_get_current_record().valid);

  ASSERT_EQ(publish(true), 0);
  const mdns_knx_record_t record = knx_dns_sd_get_current_record();
  EXPECT_TRUE(record.valid);
  EXPECT_STREQ(record.knx.sn, SERIAL);
  EXPECT_EQ(record.knx.iid, IID);
  EXPECT_EQ(record.knx.ia, IA);
  EXPECT_TRUE(record.knx.pm);
  EXPECT_EQ(record.knx.sp, 0);

  ASSERT_EQ(publish(false), 0);
  EXPECT_FALSE(knx_dns_sd_get_current_record().knx.pm);
}

/* -- query responder (listener thread) ------------------------------------- */

TEST_F(DnsSd, PtrQueryForTheServiceTypeGetsThePtrAnswer)
{
  ASSERT_EQ(publish(false), 0);

  CapturedPacket answer;
  ASSERT_TRUE(ask("_knx._udp.local.", DNS_TYPE_PTR, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_PTR);
  EXPECT_EQ(answer.first_name, "_knx._udp.local");
  EXPECT_GT(answer.first_ttl, 0u);
  EXPECT_TRUE(contains(answer.bytes, SERIAL));
}

TEST_F(DnsSd, AnyQueryForTheServiceTypeGetsThePtrAnswer)
{
  ASSERT_EQ(publish(false), 0);

  CapturedPacket answer;
  ASSERT_TRUE(ask("_knx._udp.local.", DNS_TYPE_ANY, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_PTR);
  EXPECT_EQ(answer.first_name, "_knx._udp.local");
}

TEST_F(DnsSd, PtrQueryForTheSerialSubtypeGetsThePtrAnswer)
{
  ASSERT_EQ(publish(false), 0);

  const std::string subtype = std::string("_") + SERIAL + "._sub._knx._udp.local";
  CapturedPacket answer;
  ASSERT_TRUE(ask((subtype + ".").c_str(), DNS_TYPE_PTR, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_PTR);
  EXPECT_EQ(answer.first_name, subtype);
}

TEST_F(DnsSd, PtrQueryForTheIaSubtypeGetsThePtrAnswer)
{
  ASSERT_EQ(publish(false), 0);

  const std::string subtype = "_ia1199887766-1101._sub._knx._udp.local";
  CapturedPacket answer;
  ASSERT_TRUE(ask((subtype + ".").c_str(), DNS_TYPE_PTR, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_PTR);
  EXPECT_EQ(answer.first_name, subtype);
}

TEST_F(DnsSd, PtrQueryForThePmSubtypeIsAnsweredOnlyInProgrammingMode)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  size_t base = packet_count();

  queue_query("_pm._sub._knx._udp.local.", DNS_TYPE_PTR);
  expect_no_more_packets(base);

  ASSERT_EQ(publish(true), 0);
  advance_ms(0);

  CapturedPacket answer;
  ASSERT_TRUE(ask("_pm._sub._knx._udp.local.", DNS_TYPE_PTR, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_PTR);
  EXPECT_EQ(answer.first_name, "_pm._sub._knx._udp.local");
}

TEST_F(DnsSd, SrvQueryForTheInstanceGetsTheSrvAnswer)
{
  ASSERT_EQ(publish(false), 0);

  const std::string instance = std::string(SERIAL) + "._knx._udp.local";
  CapturedPacket answer;
  ASSERT_TRUE(ask((instance + ".").c_str(), DNS_TYPE_SRV, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_SRV);
  EXPECT_EQ(answer.first_name, instance);
  EXPECT_TRUE(contains(answer.bytes, "knx-00fa10020800")) << "the SRV target lacks the hostname";
}

TEST_F(DnsSd, AaaaQueryForTheHostnameGetsTheAaaaAnswer)
{
  ASSERT_EQ(publish(false), 0);

  CapturedPacket answer;
  ASSERT_TRUE(ask("knx-00fa10020800.local.", DNS_TYPE_AAAA, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_AAAA);
  EXPECT_EQ(answer.first_name, "knx-00fa10020800.local");

  /* rdata of the fake loopback endpoint: 15 zero bytes and a 1 */
  const uint8_t loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  bool found = false;
  for (size_t i = 0; i + sizeof(loopback) <= answer.bytes.size(); i++) {
    if (memcmp(&answer.bytes[i], loopback, sizeof(loopback)) == 0) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "the AAAA answer lacks the endpoint address";
}

TEST_F(DnsSd, AnApplicationSetHostnameIsAnnounced)
{
  /* the application may set its own host name before the first announcement */
  ASSERT_EQ(oc_core_set_device_hostname("my-device"), 0);
  ASSERT_EQ(publish(false), 0);

  const std::string instance = std::string(SERIAL) + "._knx._udp.local";
  CapturedPacket answer;
  ASSERT_TRUE(ask((instance + ".").c_str(), DNS_TYPE_SRV, &answer)) << "no answer";
  EXPECT_TRUE(contains(answer.bytes, "my-device")) << "the SRV target lacks the application host name";
  EXPECT_FALSE(contains(answer.bytes, "knx-00fa10020800")) << "the SRV target still uses the default";

  ASSERT_TRUE(ask("my-device.local.", DNS_TYPE_AAAA, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_AAAA);
  EXPECT_EQ(answer.first_name, "my-device.local");
}

TEST_F(DnsSd, TxtQueryForTheInstanceGetsTheTxtAnswer)
{
  ASSERT_EQ(publish(false), 0);
  knx_dns_sd_set_sleep_period(30);

  const std::string instance = std::string(SERIAL) + "._knx._udp.local";
  CapturedPacket answer;
  ASSERT_TRUE(ask((instance + ".").c_str(), DNS_TYPE_TXT, &answer)) << "no answer";
  EXPECT_EQ(answer.first_type, DNS_TYPE_TXT);
  EXPECT_EQ(answer.first_name, instance);
  EXPECT_TRUE(contains(answer.bytes, "SP=30"));
}

TEST_F(DnsSd, QueryForAForeignNameGetsNoAnswer)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  const size_t base = packet_count();

  queue_query("_http._tcp.local.", DNS_TYPE_PTR);
  queue_query("other._knx._udp.local.", DNS_TYPE_SRV);
  queue_query("knx-ffffffffffff.local.", DNS_TYPE_AAAA);
  expect_no_more_packets(base);
}

TEST_F(DnsSd, QueryForAnUnhandledTypeGetsNoAnswer)
{
  ASSERT_EQ(publish(false), 0);
  advance_ms(0);
  const size_t base = packet_count();

  const uint16_t DNS_TYPE_A = 1;
  queue_query("knx-00fa10020800.local.", DNS_TYPE_A);
  expect_no_more_packets(base);
}

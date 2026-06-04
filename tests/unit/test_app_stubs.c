/*
 * Stubs for application-provided callbacks required by kisClientServer.
 *
 * oc_knx.c calls app_get_password() during the SPAKE2+ handshake.
 * In test builds the actual application is absent, so we provide a
 * dummy implementation here.
 */

const char *app_get_password(void)
{
  return "2X4W3TE0DFLLS19Y1FCH";
}

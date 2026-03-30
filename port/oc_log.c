/*
// Copyright (c) 2022-2023 Cascoda Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#ifdef __ZEPHYR__
#include <zephyr/logging/log.h>
/* LOG_MODULE_REGISTER
 * Creates the Zephyr log module "knx_iot".
 *
 * Compile-time maximum level:
 *   - LOG_LEVEL_DBG (4) when CONFIG_KNXIOT_DEBUG or CONFIG_KNXIOT_DEBUG_OSCORE
 *     is set — enables `log enable dbg knx_iot` at runtime without a rebuild.
 *   - CONFIG_KNXIOT_LOG_LEVEL otherwise (default: INF).
 *
 * The initial runtime level is clamped to CONFIG_LOG_DEFAULT_LEVEL regardless,
 * so debug output is not shown until explicitly enabled via the Zephyr shell:
 *   > log enable dbg knx_iot
 *
 * All translation units that include port/oc_log.h reference this module via
 * LOG_MODULE_DECLARE. Zephyr's logging subsystem serialises output through a
 * lock-free ring buffer so multiple threads can call LOG_* simultaneously
 * without their output lines interleaving.
 */
#if defined(CONFIG_KNXIOT_DEBUG) || defined(CONFIG_KNXIOT_DEBUG_OSCORE)
LOG_MODULE_REGISTER(knx_iot, LOG_LEVEL_DBG);
#else
LOG_MODULE_REGISTER(knx_iot, CONFIG_KNXIOT_LOG_LEVEL);
#endif
#endif /* __ZEPHYR__ */

#if defined(OC_PRINT) && defined(KNX_LOG_TO_FILE)

#include <stdarg.h>
#include <stdio.h>
#include "oc_log.h"

#define OUTPUT_FILE_NAME "stack_print_output.txt"

static FILE *ptr_to_file = NULL;

void oc_file_print(char *format, ...)
{
  if (ptr_to_file == NULL) 
  {
    ptr_to_file = fopen(OUTPUT_FILE_NAME, "w");
  }
  if (ptr_to_file) 
  {
    va_list args;
    va_start(args, format);
    (void)vfprintf(ptr_to_file, format, args);
    va_end(args);
    // flush the file
    (void)fflush(ptr_to_file);
  }
}

#endif

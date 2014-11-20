/*  Copyright (c) 2014 Alexander Sabourenkov (screwdriver@lxnt.info)

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any
    damages arising from the use of this software.

    Permission is granted to anyone to use this software for any
    purpose, including commercial applications, and to alter it and
    redistribute it freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must
    not claim that you wrote the original software. If you use this
    software in a product, an acknowledgment in the product documentation
    would be appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and
    must not be misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#include <stdio.h>
#include <stdlib.h>

#include "zhban.h"
#include "zhban-internal.h"

const char *llname[] = { "null", "fatal", "error", "warn ", "info ", "trace" };
void printfsink(const int level, const char *fmt, va_list ap) {
    fprintf(stderr, "[%s] ", llname[level < 0 ? 0 : (level > 5 ? 5: level)]);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void logrintf(int msg_level, int cur_level, logsink_t log_sink, const char *fmt, ...) {
    if (msg_level <= cur_level) {
        va_list ap;
        va_start(ap, fmt);
        log_sink(msg_level, fmt, ap);
        va_end(ap);
    }
    if (msg_level == ZHLOG_FATAL)
        abort();
}

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

#if !defined(ZHBAN_INTERNAL_H)
#define ZHBAN_INTERNAL_H

#define log_trace(obj, fmt, args...) do { logrintf(ZHLOG_TRACE, (obj)->log_level, (obj)->log_sink, "%s(): " fmt, __func__, ## args); } while(0)
#define log_info(obj, fmt, args...)  do { logrintf(ZHLOG_INFO,  (obj)->log_level, (obj)->log_sink, "%s(): " fmt, __func__, ## args); } while(0)
#define log_warn(obj, fmt, args...)  do { logrintf(ZHLOG_WARN,  (obj)->log_level, (obj)->log_sink, "%s(): " fmt, __func__, ## args); } while(0)
#define log_error(obj, fmt, args...) do { logrintf(ZHLOG_ERROR, (obj)->log_level, (obj)->log_sink, "%s(): " fmt, __func__, ## args); } while(0)
#define log_fatal(obj, fmt, args...) do { logrintf(ZHLOG_FATAL, (obj)->log_level, (obj)->log_sink, "%s(): " fmt, __func__, ## args); } while(0)

void printfsink(const int level, const char *fmt, va_list ap);
void logrintf(int msg_level, int cur_level, logsink_t log_sink, const char *fmt, ...);

#if defined(__GNUC__) || defined(__clang__)
# define ATTR_UNUSED __attribute__((unused))
#else
# define ATTR_UNUSED
#endif

#endif

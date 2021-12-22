/* Copyright (c) 2014-2018 by Mercer Road Corp
*
* Permission to use, copy, modify or distribute this software in binary or source form
* for any purpose is allowed only under explicit prior consent in writing from Mercer Road Corp
*
* THE SOFTWARE IS PROVIDED "AS IS" AND MERCER ROAD CORP DISCLAIMS
* ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL MERCER ROAD CORP
* BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
* DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
* PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
* ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
* SOFTWARE.
*/
#include "uri.h"
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include "types.h"

namespace VivoxClientApi {
Uri::Uri()
{
    m_data[0] = 0;
    m_protocol = ProtocolTypeNone;
}

// https://stackoverflow.com/a/4770992/814297
static bool prefix(const char *str, const char *pre)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

Uri::Uri(const char *uri)
{
    m_data[0] = 0;
    m_protocol = ProtocolTypeNone;
    if (uri) {
        size_t len = strlen(uri);
        size_t l = sizeof(m_data) - 1;
        if (len > l) {
            return;
        }
        if (prefix(uri, "https://")) {
            m_protocol = ProtocolTypeHttps;
        } else if (prefix(uri, "sip:")) {
            m_protocol = ProtocolTypeSip;
        } else if (prefix(uri, "http://")) {
            m_protocol = ProtocolTypeHttp;
        } else {
            return;
        }
        strncpy(m_data, uri, l);
        m_data[l] = 0;
    }
}

bool Uri::IsValid() const { return m_protocol != ProtocolTypeNone; }
void Uri::Clear() { m_protocol = ProtocolTypeNone; m_data[0] = 0; }

bool Uri::operator==(const Uri &uri) const
{
    return !strcmp(uri.m_data, m_data);
}
bool Uri::operator!=(const Uri &uri) const
{
    return !this->operator==(uri);
}
Uri &Uri::operator=(const Uri &uri)
{
    if (this == &uri) {
        return *this;
    }
    m_protocol = uri.m_protocol;
    strcpy_s(m_data, uri.m_data);
    return *this;
}

bool Uri::operator<(const Uri &RHS) const
{
    return strcmp(m_data, RHS.m_data) < 0;
}
}

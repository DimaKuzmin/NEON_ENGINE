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

#include "../stdafx.h"
#include "accountname.h"
#include <string.h>
#include <assert.h>
#include "types.h"

namespace VivoxClientApi {
AccountName::AccountName()
{
    m_name[0] = 0;
    m_bIsValid = false;
}

AccountName::AccountName(const char *name)
{
    if (name == NULL || name[0] == 0) {
        m_name[0] = 0;
        m_bIsValid = true;
        return;
    }

    size_t l = sizeof(m_name) - 1;
    assert(strlen(name) <= l);
    if (strlen(name) > l) {
        m_name[0] = 0;
        m_bIsValid = false;
        return;
    }

    strncpy(m_name, name, l);
    m_name[l] = 0;
    m_bIsValid = true;
}

bool AccountName::operator==(const AccountName &RHS) const
{
    return m_bIsValid == RHS.m_bIsValid && strcmp(m_name, RHS.m_name) == 0;
}

AccountName &AccountName::operator=(const AccountName &RHS)
{
    strcpy_s(m_name, RHS.m_name);
    m_bIsValid = RHS.m_bIsValid;
    return *this;
}

bool AccountName::operator<(const AccountName &RHS) const
{
    return strcmp(m_name, RHS.m_name) < 0;
}
}

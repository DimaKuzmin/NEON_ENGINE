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

# include "memallocators.h"
# include <malloc.h>
# include <memory.h>

#   define update_allocated_by_allocators(x)


# ifdef VX_TEST_MEM_ALLOCATORS
static const size_t alignment = 32;

extern "C" {
int gAllocated = 0;
int gMaxmem    = 0;
int gCount     = 0;

void *pf_malloc_func(size_t bytes)
{
    char *p = (char *)malloc(bytes + alignment);
    if (p == NULL) {
        return p;
    }
    *(int *)p = (int)bytes;
    gAllocated += (int)bytes;
    update_allocated_by_allocators((int)bytes);
    if (gAllocated > gMaxmem) {
        gMaxmem = gAllocated;
        // printf("current allocated heap = %d, peak allocated heap = %d, allocation count = %d\n", gAllocated, gMaxmem, gCount);
    }
    gCount++;
    // if(gCount % 500 == 0)
    // printf("current allocated heap = %d, peak allocated heap = %d, allocation count = %d\n", gAllocated, gMaxmem, gCount);
    return p + alignment;
}

void pf_free_func(void *memory)
{
    char *p = (char *)memory;
    p -= alignment;
    update_allocated_by_allocators(-*(int *)p);
    gAllocated -= *(int *)p;
    free(p);
}

void *pf_realloc_func(void *memory, size_t bytes)
{
    if (!memory) {
        return pf_malloc_func(bytes);
    }
    char *p = (char *)memory;
    p -= alignment;
    gAllocated -= *(int *)p;
    gAllocated += (int)bytes;
    update_allocated_by_allocators((int)bytes - *(int *)p);
    p = (char *)realloc(p, bytes + alignment);
    *(int *)p = (int)bytes;
    return p + alignment;
}

void *pf_calloc_func(size_t num, size_t bytes)
{
    void *p = pf_malloc_func(num * bytes);
    memset(p, 0, num * bytes);
    return p;
}

void *pf_malloc_aligned_func(size_t _alignment, size_t size)
{
    // do not use CHECK* macro here, cause it stays on in release
    //
    // TODO: commented out cause `VivoxCore::MediaPayload::MediaPayload`
    // wants block aligned by 128 bytes bound and malloc doesn't guarantee
    // it allocates block with such granularity. Should we use functions from
    // <https://www.gnu.org/software/libc/manual/html_node/Aligned-Memory-Blocks.html>
    // instead? What about other platforms?
    //
    // assert(alignment <= ::alignment);
    (void)_alignment;
    return pf_malloc_func(size);
}

void pf_free_aligned_func(void *memory)
{
    pf_free_func(memory);
}
}
# endif // VX_TEST_MEM_ALLOCATORS

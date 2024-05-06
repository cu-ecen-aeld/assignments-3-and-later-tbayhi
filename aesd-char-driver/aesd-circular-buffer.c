/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */
    struct aesd_buffer_entry *retval = NULL;

    uint8_t index;
    
    printf("\n\npassed %lu\n", char_offset);

    struct aesd_buffer_entry *curr = &(buffer->entry[buffer->out_offs]);

    if (curr) {
        AESD_CIRCULAR_BUFFER_FOREACH(curr, buffer, index) {
            printf("size %lu\n", curr->size);

            if (char_offset > curr->size - 1) {
                char_offset = char_offset - curr->size;
                printf("char_offset %lu\n", char_offset);
            } else {
                // we're here. populate the ptr w/ char and return this entry
                printf("found %s\n", curr->buffptr);
                retval = curr;

                size_t as_size_t = (size_t)(curr->buffptr[char_offset]);
                printf("char there is '%c'\n", curr->buffptr[char_offset]);
                printf("ascii val is %lu\n", as_size_t);

                entry_offset_byte_rtn = &as_size_t;

                break;
            }
        }
    }

    return retval;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    printf("\nadding '%s'\n", add_entry->buffptr);
    printf("in_offs %i\n", buffer->in_offs);
    printf("out_offs %i\n", buffer->out_offs);

    buffer->entry[buffer->in_offs] = *add_entry;

    uint8_t new_wpos = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    buffer->in_offs = new_wpos;

    if (new_wpos == buffer->out_offs) {
        uint8_t new_rpos = (new_wpos + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        buffer->out_offs = new_rpos;
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

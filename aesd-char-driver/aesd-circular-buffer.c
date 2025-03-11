#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/slab.h>  // ADD THIS FOR kfree()
#else
#include <string.h>
#include <stdlib.h>  // ADD THIS FOR free() IN USER SPACE
#endif

#include "aesd-circular-buffer.h"
#include <stdio.h>

void print_buffer(struct aesd_circular_buffer *buffer) {
    printf("Buffer state: in_offs=%d, out_offs=%d, full=%d\n", buffer->in_offs, buffer->out_offs, buffer->full);
    for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        printf("Entry %d: %s\n", i, buffer->entry[i].buffptr ? buffer->entry[i].buffptr : "NULL");
    }
}
uint8_t nextPtr(uint8_t ptr) {
   ptr++;
  if (ptr == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) // if the next pointer is equal to buffer max depth
  return 0; // wrap back to 0
  else
  return ptr; // advance
 } 
/**
 * Finds the entry at the given character offset in the buffer.
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer, size_t char_offset, size_t *entry_offset_byte_rtn)
{
    if (!buffer || !entry_offset_byte_rtn) {
        return NULL; // Invalid input
    }

    size_t chars_traversed = 0;
    uint8_t index = buffer->out_offs; // Start at the oldest entry

    do {
        struct aesd_buffer_entry *entryptr = &buffer->entry[index];

        if (entryptr->buffptr && entryptr->size > 0) { // Ensure valid entry
            if (char_offset < chars_traversed + entryptr->size) {
                *entry_offset_byte_rtn = char_offset - chars_traversed;
                return entryptr;
            }

            chars_traversed += entryptr->size;
        }

        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; // Move to the next entry

    } while (index != buffer->in_offs); // Stop when reaching the latest entry

    return NULL; // If offset is not found
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
 if((buffer->in_offs == buffer->out_offs)&&(buffer->full ==true))  //buffer is full so overwrite the old value
   {
		   buffer->entry[buffer->in_offs]= *add_entry ; //overwrite the oldest value
	           buffer->out_offs =nextPtr(buffer->out_offs); //increment both pointers
		   buffer->in_offs =nextPtr(buffer->in_offs) ; 
   } 
   else  //buffer is empty so add entries
   {
	   buffer->entry[buffer->in_offs]= *add_entry ;//add element structure at the input pointer
	   buffer->in_offs =nextPtr(buffer->in_offs) ; //increment the input pointer		  
   }
   buffer->full =(buffer->in_offs == buffer->out_offs); //check if buffer is full
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}


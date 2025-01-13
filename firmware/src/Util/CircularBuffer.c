/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.c

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */
#include "CircularBuffer.h"
#include <stddef.h>
#include "string.h"
#include <stdlib.h>
#include "osal/osal.h"

#define	max(a,b)	(((a) > (b)) ? (a) : (b))
#define	min(a,b)	(((a) < (b)) ? (a) : (b))
/* This section lists the other files that are included in this file.
 */
#define BUF_START   cirbuf->buf_ptr
#define BUF_END    (cirbuf->buf_ptr + cirbuf->buf_size - 1)

void CircularBuf_Init(CircularBuf_t* cirbuf, int(*fp)(uint8_t*,uint16_t), uint16_t size)
{
    cirbuf->buf_ptr                = OSAL_Malloc(size);
    cirbuf->process_callback       = fp;
    cirbuf->buf_size               = size;
    cirbuf->insertPtr              = cirbuf->removePtr = cirbuf->buf_ptr;
    cirbuf->totalBytes             = 0;
}

/*=============================================================================
 * Function: CircularBuf_GetPendingBytes
 * Execution Level:
 * Execution Time: Not measured
 *
 * Return Value:  None
 *
 * Abstract: Returns the number of bytes waiting in the transmit buffer.  
 *           To prevent returning a bad value if an ISR happens to change logger.txBuf.totalBytes 
 *           while we are reading it, we read until we get the same answer twice in a row.  
 *           This method allows us to get a good reading without turning off interrupts to do it.
 *===========================================================================*/
uint16_t CircularBuf_NumBytesAvailable(CircularBuf_t* cirbuf)
{

    uint16_t num1,num2;
    do{
      num1 = cirbuf->totalBytes;
      num2 = cirbuf->totalBytes;
    }while(num1!= num2);
    
    return num1;
}

uint16_t CircularBuf_NumBytesFree(CircularBuf_t* cirbuf)
{
    return (cirbuf->buf_size - CircularBuf_NumBytesAvailable(cirbuf));
}


uint16_t CircularBuf_ProcessBytes(CircularBuf_t* cirbuf, uint8_t* bytesBuf, uint16_t maxBytes, int* error)
{
    uint16_t bytesToSend  = 0;
    uint16_t bytesRemoved = 0; 
    *error = 0;
   
    bytesToSend = min(CircularBuf_NumBytesAvailable(cirbuf), maxBytes);
   
    if(bytesToSend>0){
        
        if(bytesBuf != NULL){
                                
            //start next transfer
            //make sure we don't transfer the out of bound data.
            if((cirbuf->removePtr + bytesToSend) > BUF_END){
                uint16_t chunk1 = 0;
                uint16_t chunk2 = 0;

                // transfer the first part of data
                chunk1 = (BUF_END - cirbuf->removePtr + 1);

                memcpy(bytesBuf, cirbuf->removePtr, chunk1);

                // wrap-around the pointer to the begining of the buffer,
                // then transfer second part of data. 
                cirbuf->removePtr = BUF_START;
                bytesRemoved += chunk1;

                chunk2 = bytesToSend - chunk1;
                memcpy(bytesBuf+chunk1, cirbuf->removePtr,chunk2);
                cirbuf->removePtr += chunk2;

                bytesRemoved += chunk2;
            }
            else{        
                memcpy(bytesBuf, cirbuf->removePtr, bytesToSend);
                bytesRemoved = bytesToSend;
                cirbuf->removePtr += bytesRemoved;
            } 
            cirbuf->totalBytes  -= bytesRemoved;

        }
        else if(cirbuf->process_callback!= NULL){
                 
            //start next transfer
            //make sure we don't transfer the out of bound data.
            if((cirbuf->removePtr + bytesToSend) > BUF_END){

                // transfer the first part of data
                bytesToSend = (BUF_END - cirbuf->removePtr + 1);     
                *error = cirbuf->process_callback(cirbuf->removePtr, bytesToSend);

                // wrap-around the pointer to the begining of the buffer,
                // then transfer second part of data. 
                cirbuf->removePtr = BUF_START;
            }
            else{

                *error = cirbuf->process_callback(cirbuf->removePtr, bytesToSend);
                cirbuf->removePtr += bytesToSend;
            } 
            cirbuf->totalBytes -= bytesToSend;
            bytesRemoved  = bytesToSend;
        }
    }
    
    return bytesRemoved;
}

/*======================================================================================
  CircularBuf_AddBytes
  Send out CF UDP Server message. Returns number of bytes sent.
  =====================================================================================*/
uint16_t CircularBuf_AddBytes(CircularBuf_t* cirbuf, uint8_t* bytesBuf, uint16_t bytesToSend)
{
    int numBytesCopied = 0;
    int wMaxPut;
    uint8_t* bytesPtr;

    
    if(bytesToSend>0){
  
        // check how many bytes were already taken in the logging buffer. 
        // queue up the the buffer limit. If the buffer is almost full and can't fit
        // the entire message, discard it 
        if(CircularBuf_NumBytesFree(cirbuf) >= bytesToSend){

            numBytesCopied = bytesToSend; // make a copy of the total bytes to send. 
            bytesPtr       = bytesBuf;
         
            // copy the data over.  Note that a two part memcpy 
            // may be needed if the data spans the buffer end to start address.
            wMaxPut = BUF_END - cirbuf->insertPtr + 1;
            
            if(wMaxPut<bytesToSend){
                // Do the first memcpy to copy until the end of buffer. 
                memcpy(cirbuf->insertPtr, bytesPtr, wMaxPut);

                bytesPtr += wMaxPut;
                bytesToSend -= wMaxPut;
                // wrap the insert pointer to the beginning of the buffer. 
                cirbuf->insertPtr = BUF_START;
            }
            memcpy(cirbuf->insertPtr, bytesPtr, bytesToSend);
            cirbuf->insertPtr  += bytesToSend;
            cirbuf->totalBytes += numBytesCopied;    //Update total number of bytes in the Queue.
        }
        else{
            return 0;   //buffer is full. Return 0.
        }
    }

    return numBytesCopied;
}
void CircularBuf_Reset(CircularBuf_t* cirbuf)
{
    if (cirbuf != NULL) {
        // Reset the insert and remove pointers to the start of the buffer
        cirbuf->insertPtr = cirbuf->removePtr = cirbuf->buf_ptr;
        // Reset the total byte count to zero
        cirbuf->totalBytes = 0;
    }
}
/* *****************************************************************************
 End of File
 */

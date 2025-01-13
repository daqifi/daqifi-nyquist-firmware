/*
Author: Samoylov Eugene aka Helius (ghelius@gmail.com)
BUGS and TODO:
-- add echo_off feature
-- rewrite history for use more than 256 byte buffer
*/
#include "microrl.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#ifdef _USE_LIBC_STDIO
#include <stdio.h>
#endif

//#define DBG(...) fprintf(stderr, "\033[33m");fprintf(stderr,__VA_ARGS__);fprintf(stderr,"\033[0m");
#ifdef _USE_HISTORY

#ifdef _HISTORY_DEBUG
//*****************************************************************************
// print buffer content on screen
static void print_hist (ring_history_t * pThis)
{
	printf ("\n");
	for (int i = 0; i < _RING_HISTORY_LEN; i++) {
		if (i == pThis->begin)
			printf ("b");
		else 
			printf (" ");
	}
	printf ("\n");
	for (int i = 0; i < _RING_HISTORY_LEN; i++) {
		if (isalpha(pThis->ring_buf[i]))
			printf ("%c", pThis->ring_buf[i]);
		else 
			printf ("%d", pThis->ring_buf[i]);
	}
	printf ("\n");
	for (int i = 0; i < _RING_HISTORY_LEN; i++) {
		if (i == pThis->end)
			printf ("e");
		else 
			printf (" ");
	}
	printf ("\n");
}
#endif

//*****************************************************************************
// remove older message from ring buffer
static void hist_erase_older (ring_history_t * pThis)
{
	int new_pos = pThis->begin + pThis->ring_buf [pThis->begin] + 1;
	if (new_pos >= _RING_HISTORY_LEN)
		new_pos = new_pos - _RING_HISTORY_LEN;
	
	pThis->begin = new_pos;
}

//*****************************************************************************
// check space for new line, remove older while not space
static int hist_is_space_for_new (ring_history_t * pThis, int len)
{
	if (pThis->ring_buf [pThis->begin] == 0)
		return true;
	if (pThis->end >= pThis->begin) {
		if (_RING_HISTORY_LEN - pThis->end + pThis->begin - 1 > len)
			return true;
	}	else {
		if (pThis->begin - pThis->end - 1> len)
			return true;
	}
	return false;
}

//*****************************************************************************
// put line to ring buffer
static void hist_save_line (ring_history_t * pThis, char * line, int len)
{
	if (len > _RING_HISTORY_LEN - 2)
		return;
	while (!hist_is_space_for_new (pThis, len)) {
		hist_erase_older (pThis);
	}
	// if it's first line
	if (pThis->ring_buf [pThis->begin] == 0) 
		pThis->ring_buf [pThis->begin] = len;
	
	// store line
	if (len < _RING_HISTORY_LEN-pThis->end-1)
		memcpy (pThis->ring_buf + pThis->end + 1, line, len);
	else {
		int part_len = _RING_HISTORY_LEN-pThis->end-1;
		memcpy (pThis->ring_buf + pThis->end + 1, line, part_len);
		memcpy (pThis->ring_buf, line + part_len, len - part_len);
	}
	pThis->ring_buf [pThis->end] = len;
	pThis->end = pThis->end + len + 1;
	if (pThis->end >= _RING_HISTORY_LEN)
		pThis->end -= _RING_HISTORY_LEN;
	pThis->ring_buf [pThis->end] = 0;
	pThis->cur = 0;
#ifdef _HISTORY_DEBUG
	print_hist (pThis);
#endif
}

//*****************************************************************************
// copy saved line to 'line' and return size of line
static int hist_restore_line (ring_history_t * pThis, char * line, int dir)
{
	int cnt = 0;
	// count history record	
	int header = pThis->begin;
	while (pThis->ring_buf [header] != 0) {
		header += pThis->ring_buf [header] + 1;
		if (header >= _RING_HISTORY_LEN)
			header -= _RING_HISTORY_LEN; 
		cnt++;
	}

	if (dir == _HIST_UP) {
		if (cnt >= pThis->cur) {
			int header = pThis->begin;
			int j = 0;
			// found record for 'pThis->cur' index
			while ((pThis->ring_buf [header] != 0) && (cnt - j -1 != pThis->cur)) {
				header += pThis->ring_buf [header] + 1;
				if (header >= _RING_HISTORY_LEN)
					header -= _RING_HISTORY_LEN;
				j++;
			}
			if (pThis->ring_buf[header]) {
					pThis->cur++;
				// obtain saved line
				if (pThis->ring_buf [header] + header < _RING_HISTORY_LEN) {
					memset (line, 0, _COMMAND_LINE_LEN);
					memcpy (line, pThis->ring_buf + header + 1, pThis->ring_buf[header]);
				} else {
					int part0 = _RING_HISTORY_LEN - header - 1;
					memset (line, 0, _COMMAND_LINE_LEN);
					memcpy (line, pThis->ring_buf + header + 1, part0);
					memcpy (line + part0, pThis->ring_buf, pThis->ring_buf[header] - part0);
				}
				return pThis->ring_buf[header];
			}
		}
	} else {
		if (pThis->cur > 0) {
				pThis->cur--;
			int header = pThis->begin;
			int j = 0;

			while ((pThis->ring_buf [header] != 0) && (cnt - j != pThis->cur)) {
				header += pThis->ring_buf [header] + 1;
				if (header >= _RING_HISTORY_LEN)
					header -= _RING_HISTORY_LEN;
				j++;
			}
			if (pThis->ring_buf [header] + header < _RING_HISTORY_LEN) {
				memcpy (line, pThis->ring_buf + header + 1, pThis->ring_buf[header]);
			} else {
				int part0 = _RING_HISTORY_LEN - header - 1;
				memcpy (line, pThis->ring_buf + header + 1, part0);
				memcpy (line + part0, pThis->ring_buf, pThis->ring_buf[header] - part0);
			}
			return pThis->ring_buf[header];
		} else {
			/* empty line */
			return 0;
		}
	}
	return -1;
}
#endif

#ifdef USE_SPLIT
//*****************************************************************************
// split cmdline to tkn array and return nmb of token
static int split (microrl_t* pThis, int limit, char const ** tkn_arr)
{
	int i = 0;
	int ind = 0;
	while (1) {
		// go to the first whitespace (zerro for us)
		while ((pThis->cmdline [ind] == '\0') && (ind < limit)) {
			ind++;
		}
		if (!(ind < limit)) return i;
		tkn_arr[i++] = pThis->cmdline + ind;
		if (i >= _COMMAND_TOKEN_NMB) {
			return -1;
		}
		// go to the first NOT whitespace (not zero for us)
		while ((pThis->cmdline [ind] != '\0') && (ind < limit)) {
			ind++;
		}
		if (!(ind < limit)) return i;
	}
	return i;
}
#endif

/**
 * Prints to the terminal only
 * @param pThis The microrl instance
 * @param dataSize The size of the string
 * @param data The string to print
 * @param cursorSize The amount to shift the cursor
 */
static void terminal_print(microrl_t* pThis, size_t dataSize, char* data, int terminalSize)
{
    pThis->print(pThis, dataSize, data);
    pThis->terminalPos += terminalSize;
    if (pThis->terminalPos < 0)
    {
        pThis->terminalPos = 0;
    }
}

#ifndef _USE_LIBC_STDIO
//*****************************************************************************
// convert 16 bit value to string
// 0 value not supported!!! just make empty string
// Returns pointer to a buffer tail
static char *u16bit_to_str (unsigned int nmb, char * buf)
{
	char tmp_str [6] = {0,};
	int i = 0, j;
	if (nmb <= 0xFFFF) {
		while (nmb > 0) {
			tmp_str[i++] = (nmb % 10) + '0';
			nmb /=10;
		}
		for (j = 0; j < i; ++j)
			*(buf++) = tmp_str [i-j-1];
	}
	*buf = '\0';
	return buf;
}
#endif

/**
 * Resets the cursor for the terminal only
 * @param pThis The microrl instance
 */
static void terminal_move_cursor(microrl_t * pThis, int offset)
{
   // Build the terminal command
	char str[16] = {0,};
#ifdef _USE_LIBC_STDIO 
	if (offset > 0) {
		snprintf (str, 16, "\033[%dC", offset);
	} else if (offset < 0) {
		snprintf (str, 16, "\033[%dD", -(offset));
	}
#else 
	char *endstr;
	strcpy (str, "\033[");
	if (offset > 0) {
		endstr = u16bit_to_str (offset, str+2);
		strcpy (endstr, "C");
	} else if (offset < 0) {
		endstr = u16bit_to_str (-(offset), str+2);
		strcpy (endstr, "D");
	} else
		return;
#endif
    
    terminal_print(pThis, strlen(str), str, offset);
}

/**
 * Resets the cursor for the terminal only
 * @param pThis The microrl instance
 */
static void terminal_reset_cursor(microrl_t * pThis)
{
    terminal_move_cursor(pThis, -1 * pThis->terminalPos);
}

/**
 * Prints to the terminal only
 * @param pThis The microrl instance
 */
static void terminal_print_line(microrl_t* pThis)
{
    terminal_reset_cursor(pThis);
    if(pThis->echoOn > -1)  // DAQiFi modification
    {                       // DAQiFi modification
        terminal_print(pThis, strlen(pThis->prompt_str), pThis->prompt_str, pThis->prompt_size);
        terminal_print(pThis, pThis->cmdlen,  pThis->cmdline, pThis->cmdlen);
    
    
        if (pThis->dataCursor <  pThis->cmdlen)
        {
            terminal_move_cursor(pThis, pThis->dataCursor -  pThis->cmdlen);
        }
    }
}   // DAQiFi modification

/**
 * Shifts the cursor by the specified amount
 * @param pThis The microrl instance
 * @param offset
 */
static void microrl_move_cursor(microrl_t * pThis, int offset)
{
    terminal_move_cursor(pThis, offset);
            
    // Update the data cursor
    pThis->dataCursor += offset;
    if (pThis->dataCursor < 0)
    {
        pThis->dataCursor = 0;
    }
}

/**
 * Resets the cursor to the beginning of the current line
 * @param pThis The microrl instance
 */
static void microrl_reset_cursor(microrl_t * pThis)
{
    terminal_reset_cursor(pThis);
    pThis->dataCursor = 0;
}

/**
 * Moves the cursor to the end of the line
 * @param pThis The microrl instance
 */
static void microrl_cursor_end(microrl_t * pThis)
{
    int offset = pThis->cmdlen - pThis->dataCursor;
    
    terminal_move_cursor(pThis, offset);
    pThis->dataCursor = pThis->cmdlen;
}

/**
 * Removes the last character from the buffer and updates the terminal accordingly
 * @param pThis The microrl instance
 */
inline static void microrl_delete(microrl_t * pThis)
{
    // Remove a character from the terminal
    if (pThis->cmdlen > 0 && pThis->dataCursor < pThis->cmdlen)
    {
        // Special case, if its the last character, just overwrite it with \0
        // and tell the terminal to clear to the end of the line
        if (pThis->dataCursor == pThis->cmdlen - 1)
        {
            --pThis->cmdlen;
            pThis->cmdline[pThis->dataCursor] = '\0';
            
            // Tell the terminal to clear to the end
            char* command = "\033[K";
            terminal_print(pThis, strlen(command), command, 0);
        }
        else
        {
            // Otherwise we have to move all data after the cursor forward one
            
            --pThis->cmdlen;
            memmove(pThis->cmdline + pThis->dataCursor,
                    pThis->cmdline + pThis->cmdlen,
                    pThis->cmdlen - pThis->dataCursor);
            pThis->cmdline[pThis->cmdlen] = '\0'; // Make sure its remains null terminated
            
            // In this case we have to re-print the whole line
            terminal_print_line(pThis);
        }
    }
}

/**
 * Removes the last character from the buffer and updates the terminal accordingly
 * @param pThis The microrl instance
 */
inline static void microrl_backspace(microrl_t * pThis)
{
    if (pThis->dataCursor > 0)
    {
        microrl_move_cursor(pThis, -1);
        microrl_delete(pThis);
    }
}

/**
 * Clears the buffer
 * @param pThis The microrl instance
 */
inline static void microrl_clear(microrl_t* pThis)
{
    // Reset the buffer
    pThis->cmdlen = 0;
	pThis->dataCursor = 0;
	memset(pThis->cmdline, 0, _COMMAND_LINE_LEN);
    
    // Print the line
	terminal_print_line(pThis);
    
    
#ifdef _USE_HISTORY
	// Reset the history buffer
    pThis->ring_hist.cur = 0;
#endif    
}

//*****************************************************************************
void microrl_init (microrl_t * pThis, MicrorlPrintFxn print) 
{
	memset(pThis->cmdline, 0, _COMMAND_LINE_LEN);
#ifdef _USE_HISTORY
	memset(pThis->ring_hist.ring_buf, 0, _RING_HISTORY_LEN);
	pThis->ring_hist.begin = 0;
	pThis->ring_hist.end = 0;
	pThis->ring_hist.cur = 0;
#endif
	pThis->cmdlen =0;
	pThis->dataCursor = 0;
    pThis->terminalPos = 0;

	pThis->execute = NULL;
	pThis->get_completion = NULL;
#ifdef _USE_CTLR_C
	pThis->sigint = NULL;
#endif
	pThis->prompt_str = _PROMPT_DEFAULT;
    pThis->prompt_size = _PROMPT_DEFAULT_LEN;
	pThis->print = print;
    pThis->echoOn = true;
#ifdef _ENABLE_INIT_PROMPT
	print_prompt (pThis);
#endif
}

void microrl_set_echo (microrl_t* pThis, int echoOn)
{
    pThis->echoOn = echoOn;
}

//*****************************************************************************
void microrl_set_complete_callback (microrl_t * pThis, MicrorlCompleteFxn get_completion)
{
	pThis->get_completion = get_completion;
}

//*****************************************************************************
void microrl_set_execute_callback (microrl_t * pThis, MicrorlExecuteFxn execute)
{
	pThis->execute = execute;
}
#ifdef _USE_CTLR_C
//*****************************************************************************
void microrl_set_sigint_callback (microrl_t * pThis, void (*sigintf)(void))
{
	pThis->sigint = sigintf;
}
#endif

#ifdef _USE_ESC_SEQ
static void hist_search (microrl_t * pThis, int dir)
{
	int len = hist_restore_line (&pThis->ring_hist, pThis->cmdline, dir);
	if (len >= 0) {
        pThis->cmdlen = len;
        pThis->dataCursor = len;

		terminal_print_line(pThis);
	}
}

//*****************************************************************************
// handling escape sequences
static int escape_process (microrl_t * pThis, char ch)
{
	if (ch == '[') {
		pThis->escape_seq = _ESC_BRACKET;
		return 0;
	} else if (pThis->escape_seq == _ESC_BRACKET) {
		if (ch == 'A') {
#ifdef _USE_HISTORY
			hist_search (pThis, _HIST_UP);
#endif
			return 1;
		} else if (ch == 'B') {
#ifdef _USE_HISTORY
			hist_search (pThis, _HIST_DOWN);
#endif
			return 1;
		} else if (ch == 'C') {
			if (pThis->dataCursor < pThis->cmdlen) {
				microrl_move_cursor(pThis, 1);
			}
			return 1;
		} else if (ch == 'D') {
			if (pThis->dataCursor > 0) {
				microrl_move_cursor(pThis, -1);
			}
			return 1;
		} else if (ch == '7') {
			pThis->escape_seq = _ESC_HOME;
			return 0;
		} else if (ch == '8') {
			pThis->escape_seq = _ESC_END;
			return 0;
		} 
	} else if (ch == '~') {
		if (pThis->escape_seq == _ESC_HOME) {
            microrl_reset_cursor(pThis);
			return 1;
		} else if (pThis->escape_seq == _ESC_END) {
			microrl_cursor_end(pThis);
			return 1;
		}
	}

	/* unknown escape sequence, stop */
	return 1;
}
#endif

//*****************************************************************************
// insert len char of text at cursor position
static int buffer_insert_text(microrl_t * pThis, char * text, int len)
{
	if (pThis->dataCursor + len < _COMMAND_LINE_LEN)
    {
		memmove (pThis->cmdline + pThis->dataCursor, text, len);
        if (pThis->dataCursor + len > pThis->cmdlen)
        {
            pThis->cmdlen = pThis->dataCursor + len;
            pThis->dataCursor = pThis->cmdlen;
            pThis->cmdline [pThis->cmdlen] = '\0';
        }
        else
        {
            pThis->dataCursor += len;	
        }
        
		return true;
	}
    
	return false;
}

#ifdef _USE_COMPLETE

//*****************************************************************************
static int common_len (char ** arr)
{
	int i;
	int j;
	char *shortest = arr[0];
	int shortlen = strlen(shortest);

	for (i = 0; arr[i] != NULL; ++i)
		if (strlen(arr[i]) < shortlen) {
			shortest = arr[i];
			shortlen = strlen(shortest);
		}

	for (i = 0; i < shortlen; ++i)
		for (j = 0; arr[j] != 0; ++j)
			if (shortest[i] != arr[j][i])
				return i;

	return i;
}

//*****************************************************************************
static void microrl_get_complite (microrl_t * pThis) 
{
	char const * tkn_arr[_COMMAND_TOKEN_NMB];
	char ** compl_token; 
	
	if (pThis->get_completion == NULL) // callback was not set
		return;
	
	int status = split (pThis, pThis->cursor, tkn_arr);
	if (pThis->cmdline[pThis->cursor-1] == '\0')
		tkn_arr[status++] = "";
	compl_token = pThis->get_completion (status, tkn_arr);
	if (compl_token[0] != NULL) {
		int i = 0;
		int len;

		if (compl_token[1] == NULL) {
			len = strlen (compl_token[0]);
		} else {
			len = common_len (compl_token);
			terminal_newline (pThis);
			while (compl_token [i] != NULL) {
				pThis->print (strlen(compl_token[i]), compl_token[i]);
				pThis->print (1, " ");
				i++;
			}
			terminal_newline (pThis);
			print_prompt (pThis);
		}
		
		if (len) {
			microrl_insert_text (pThis, compl_token[0] + strlen(tkn_arr[status-1]), 
																	len - strlen(tkn_arr[status-1]));
			if (compl_token[1] == NULL) 
				microrl_insert_text (pThis, " ", 1);
		}
		terminal_reset_cursor (pThis);
		terminal_print_line (pThis, 0, pThis->cursor);
	} 
}
#endif

//*****************************************************************************
void new_line_handler(microrl_t * pThis){
    if(pThis->echoOn > -1)  // DAQiFi modification
    {                       // DAQiFi modification
        // Move the terminal to the next line
        terminal_print(pThis, strlen(ENDL), ENDL, 0);
        pThis->terminalPos = 0;
    }                       // DAQiFi modification
    
    // Save the command
#ifdef _USE_HISTORY
	if (pThis->cmdlen > 0)
		hist_save_line (&pThis->ring_hist, pThis->cmdline, pThis->cmdlen);
#endif
	
    // Pass it to the command processor
    // Make sure the command terminator is appended for libscpi (but e do not want this saved in the history!)
    buffer_insert_text(pThis, ENDL, strlen(ENDL));
	if (pThis->execute != NULL)
    {
		pThis->execute (pThis, pThis->cmdlen, pThis->cmdline);
    }
    
    // Start the next line
    microrl_clear(pThis);
}

//*****************************************************************************
void microrl_insert_char (microrl_t * pThis, int ch)
{
#ifdef _USE_ESC_SEQ
	if (pThis->escape) {
		if (escape_process(pThis, ch))
			pThis->escape = 0;
	} else {
#endif
		switch (ch) {
			case KEY_CR:
                // When presented with \n\r, only print one newline
                if (pThis->lastChar != KEY_LF)
                {
                    new_line_handler(pThis);
                }
                break;
			case KEY_LF:
                // When presented with \r\n, only print one newline
                if (pThis->lastChar != KEY_CR)
                {
                    new_line_handler(pThis);
                }
			break;
			//-----------------------------------------------------
#ifdef _USE_COMPLETE
			case KEY_HT:
				microrl_get_complite (pThis);
			break;
#endif
			//-----------------------------------------------------
			case KEY_ESC:
#ifdef _USE_ESC_SEQ
				pThis->escape = 1;
#endif
			break;
			//-----------------------------------------------------
			case KEY_NAK: // ^U
                microrl_clear(pThis);
			break;
			//-----------------------------------------------------
			case KEY_VT:  // ^K
                terminal_print(pThis, 3, "\033[K", 0);
			break;
			//-----------------------------------------------------
			case KEY_ENQ: // ^E
				microrl_cursor_end(pThis);
			break;
			//-----------------------------------------------------
			case KEY_SOH: // ^A
				microrl_reset_cursor(pThis);
			break;
			//-----------------------------------------------------
			case KEY_ACK: // ^F
                if (pThis->dataCursor < pThis->cmdlen) {
                     microrl_move_cursor(pThis, 1);
                }
			break;
			//-----------------------------------------------------
			case KEY_STX: // ^B
                if (pThis->dataCursor > 0) {
                    microrl_move_cursor(pThis, -1);
                }
			break;
			//-----------------------------------------------------
			case KEY_DLE: //^P
#ifdef _USE_HISTORY
                hist_search (pThis, _HIST_UP);
#endif
			break;
			//-----------------------------------------------------
			case KEY_SO: //^N
#ifdef _USE_HISTORY
                hist_search (pThis, _HIST_DOWN);
#endif
			break;
			//-----------------------------------------------------
			case KEY_DEL:
                // Many terminals send backspace as delete.
                // Since backspace is more useful, thats how we'll intrepret it.
                // microrl_delete(pThis);
                // break;
			case KEY_BS:
				microrl_backspace(pThis);
			break;
#ifdef _USE_CTLR_C
			case KEY_ETX:
			if (pThis->sigint != NULL)
				pThis->sigint();
			break;
#endif
			//-----------------------------------------------------
			default:
                if (((ch == ' ') && (pThis->cmdlen == 0))
                    || IS_CONTROL_CHAR(ch))
                {
                    break;
                }
                
                if (buffer_insert_text (pThis, (char*)&ch, 1))
                {
                    if (pThis->terminalPos == 0)
                    {
                        terminal_print_line(pThis);
                    }
                    else
                    {
                        if (pThis->echoOn > -1)
                        {
                            terminal_print(pThis, 1, (char*)&ch, 1);
                        }
                    }
                }
			
			break;
		}
#ifdef _USE_ESC_SEQ
	}
#endif
    
    // Record the last character for newline handling
    pThis->lastChar = ch;
}

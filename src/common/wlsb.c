/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file wlsb.c
 * @brief Window-based Least Significant Bits (W-LSB) encoding
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author David Moreau from TAS
 * @author The hackers from ROHC for Linux
 */

#include "wlsb.h"
#include "interval.h" /* for the f() function */
#include "rohc_traces.h"
#include "rohc_debug.h"

#include <string.h>
#include <assert.h>


/*
 * Private function prototypes:
 */

static void c_ack_remove(struct c_wlsb *s, int index);

static size_t g(const uint32_t v_ref, const uint32_t v,
                const int32_t p, const size_t bits_nr);


/*
 * Public functions
 */

/**
 * @brief Create a new Window-based Least Significant Bits (W-LSB) encoding
 *        object
 *
 * @param bits         The maximal number of bits for representing a value
 * @param window_width The number of entries in the window
 * @param p            Shift parameter (see 4.5.2 in the RFC 3095)
 * @return             The newly-created W-LSB encoding object
 */
struct c_wlsb * c_create_wlsb(const size_t bits,
                              const size_t window_width,
                              const int32_t p)
{
	struct c_wlsb *wlsb;
	size_t entry;

	assert(bits > 0);
	assert(window_width > 0);

	wlsb = malloc(sizeof(struct c_wlsb));
	if(wlsb == NULL)
	{
	  rohc_debugf(0, "cannot allocate memory for the W-LSB object\n");
	  goto error;
	}
	bzero(wlsb, sizeof(struct c_wlsb));

	wlsb->oldest = 0;
	wlsb->next = 0;
	wlsb->window_width = window_width;

	wlsb->window = (struct c_window *) calloc(window_width, sizeof(struct c_window));
	if(wlsb->window == NULL)
	{
		rohc_debugf(0, "cannot allocate memory for the W-LSB window\n");
		goto clean;
	}
	bzero(wlsb->window, sizeof(struct c_window) * window_width);

	wlsb->bits = bits;
	wlsb->p = p;
	for(entry = 0; entry < window_width; entry++)
	{
		wlsb->window[entry].is_used = 0;
	}

	return wlsb;

clean:
	zfree(wlsb);
error:
	return NULL;
}


/**
 * @brief Destroy a Window-based LSB (W-LSB) encoding object
 *
 * @param wlsb  The W-LSB object to destory
 */
void c_destroy_wlsb(struct c_wlsb *const wlsb)
{
	assert(wlsb != NULL);
	assert(wlsb->window != NULL);
	free(wlsb->window);
	free(wlsb);
}


/**
 * @brief Add a value into a W-LSB encoding object
 *
 * @param wlsb  The W-LSB object
 * @param sn    The Sequence Number (SN) for the new entry
 * @param value The value to base the LSB coding on
 */
void c_add_wlsb(struct c_wlsb *const wlsb,
                const uint16_t sn,
                const uint32_t value)
{
	assert(wlsb != NULL);
	assert(wlsb->window != NULL);
	assert(wlsb->next < wlsb->window_width);

	/* if window is full, an entry is overwritten */
	if(wlsb->window[wlsb->next].is_used)
	{
		wlsb->oldest = (wlsb->oldest + 1) % wlsb->window_width;
	}

	wlsb->window[wlsb->next].sn = sn;
	wlsb->window[wlsb->next].value = value;
	wlsb->window[wlsb->next].is_used = 1;
	wlsb->next = (wlsb->next + 1) % wlsb->window_width;
}


/**
 * @brief Find out the minimal number of bits of the to-be-encoded value
 *        required to be able to uniquely recreate it given the window
 *
 * @param wlsb     The W-LSB object
 * @param value    The value to encode using the LSB algorithm
 * @param bits_nr  OUT: The number of bits required to uniquely recreate the
 *                      value
 * @return         1 in case of success,
 *                 0 if the minimal number of bits can not be found
 */
int c_get_k_wlsb(const struct c_wlsb *const wlsb,
                 const uint32_t value,
                 size_t *const bits_nr)
{
	size_t entry;
	int valid;
	uint32_t min;
	uint32_t max;
	int is_success;
	
	assert(wlsb != NULL);
	assert(wlsb->window != NULL);

	min = 0xffffffff;
	max = 0;
	valid = 0;

	/* find out the interval in which all the values from the window stand */
	for(entry = 0; entry < wlsb->window_width; entry++)
	{
		/* skip entry if not in use */
		if(!(wlsb->window[entry].is_used))
		{
			continue;
		}

		/* the window contains at least one value */
		valid = 1;

		/* change the minimal and maximal values if appropriate */
		if(wlsb->window[entry].value < min)
			min = wlsb->window[entry].value;
		if(wlsb->window[entry].value > max)
			max = wlsb->window[entry].value;
	}

	/* if the window contained at least one value */
	if(valid)
	{
		size_t k1;
		size_t k2;

		/* find the minimal number of bits of the value required to be able
		 * to recreate it thanks to the window */

		/* find the minimal number of bits for the lower limit of the interval */
		k1 = g(min, value, wlsb->p, wlsb->bits);
		/* find the minimal number of bits for the upper limit of the interval */
		k2 = g(max, value, wlsb->p, wlsb->bits);

		/* keep the greatest one */
		*bits_nr = (k1 > k2) ? k1 : k2;

		is_success = 1;
	}
	else /* else no k matches */
	{
		is_success = 0;
	}

	return is_success;
}


/**
 * @brief Acknowledge based on the Sequence Number (SN)
 *
 * Removes all entries older than the given SN in the window.
 *
 * @param s  The W-LSB object
 * @param sn The SN to acknowledge
 */
void c_ack_sn_wlsb(struct c_wlsb *s, int sn)
{
	int found = 0;
	int i;

	/* check the W-LSB object validity */
	if(s == NULL)
		return;

	/* search for the window entry that matches the given SN
	 * starting from the oldest one */
	for(i = s->oldest; i < s->window_width; i++)
	{
		if(s->window[i].is_used && s->window[i].sn == sn)
		{
			found = 1;
			break;
		}
	}

	if(!found)
	{
		for(i = 0; i < s->oldest; i++)
		{
			if(s->window[i].is_used && s->window[i].sn == sn)
			{
				found = 1;
				break;
			}
		}
	}

	/* remove the window entry if found */
	if(found)
		c_ack_remove(s, i);
}


/**
 * @brief Compute the sum of all the values stored in the W-LSB window
 *
 * This function is used for statistics.
 *
 * @param s The W-LSB object
 * @return  The sum over the W-LSB window
 */
int c_sum_wlsb(struct c_wlsb *s)
{
	int i;
	int sum = 0;

	for(i = 0; i < s->window_width; i++)
	{
		if(s->window[i].is_used)
		{
			sum += s->window[i].value;
		}
	}

	return sum;
}


/**
 * @brief Compute the mean of all the values stored in the W-LSB window
 *
 * This function is used for statistics.
 *
 * @param s The W-LSB object
 * @return  The mean over the W-LSB window
 */
int c_mean_wlsb(struct c_wlsb *s)
{
	int i;
	int sum = 0;
	int num = 0;

	for(i = 0; i < s->window_width; i++)
	{
		if(s->window[i].is_used)
		{
			sum += s->window[i].value;
			num++;
		}
	}

	return (num > 0 ? sum / num : 0);
}


/*
 * Private functions
 */

/**
 * @brief Removes all W-LSB window entries prior to the given index
 *
 * @param s       The W-LSB object
 * @param index   The position to set as the oldest
 */
static void c_ack_remove(struct c_wlsb *s, int index)
{
	int j;

	/* check the W-LSB object validity */
	if(s == NULL)
		return;

	rohc_debugf(2, "index is %d\n", index);

	if(s->oldest == index)
	{
		/* remove only the oldest entry */
		s->window[s->oldest].is_used = 0;
	}
	else if(s->oldest < index)
	{
		/* remove all entries from oldest to (not including) index */
		for(j = s->oldest; j < index; j++)
		{
			s->window[j].is_used = 0;
		}
	}
	else /* s->oldest > index */
	{
		/* remove all entries from oldest to wrap-around and all from start
		 * to (excluding) index */
		for(j = s->oldest; j < s->window_width; j++)
		{
			s->window[j].is_used = 0;
		}
		for(j = 0; j < index; j++)
		{
			s->window[j].is_used = 0;
		}
	}

	/* fix the s->oldest pointer */
	if(index >= (s->window_width - 1))
	{
		s->oldest = 0;
	}
	else
	{
		s->oldest = index;
		if(s->oldest >= s->window_width)
			s->oldest = 0;
	}

	/* fix the s->next pointer */
	s->next = s->oldest;
	for(j = s->oldest; j < s->window_width; j++)
	{
		if(s->window[j].is_used)
		{
			s->next = (s->next + 1) % s->window_width;
		}
		else
		{
			break;
		}
	}
	for(j = 0; j < s->oldest; j++)
	{
		if(s->window[j].is_used)
		{
			s->next = (s->next + 1) % s->window_width;
		}
		else
		{
			break;
		}
	}

	if(s->oldest >= s->window_width)
		s->oldest = 0;
}


/**
 * @brief The g function as defined in the LSB calculation algorithm
 *
 * Find the minimal k value so that v falls into the interval given by
 * \f$f(v\_ref, k)\f$. See 4.5.1 in the RFC 3095.
 *
 * @param v_ref The reference value
 * @param v     The value to encode
 * @param p     The shift parameter
 * @param bits  The number of bits that may be used to represent the
 *              LSB-encoded value
 * @return      The minimal k value as defined by the LSB calculation algorithm
 */
static size_t g(const uint32_t v_ref, const uint32_t v,
                const int32_t p, const size_t bits_nr)
{
	uint32_t min;
	uint32_t max;
	size_t k;

	for(k = 0; k < bits_nr; k++)
	{
		f(v_ref, k, p, &min, &max);
		if(v >= min && v <= max)
			break;
	}

	return k;
}


/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VIRTUOSO_H_
#define VIRTUOSO_H_

#include <tas.h>

/** Turns on GRE tunneling */
#define VIRTUOSO_GRE 1 
#define VIRTUOSO_OVS 1

static inline uint64_t get_budget(int vmid)
{
  return __sync_fetch_and_add(&fp_state->budgets[vmid].budget, 0);
}

static inline void add_budget(int vmid, uint64_t val)
{
  __sync_fetch_and_add(&fp_state->budgets[vmid].budget, val);
}

static inline void sub_budget(int vmid, uint64_t val)
{
  __sync_fetch_and_sub(&fp_state->budgets[vmid].budget, val);
}

#endif /* ndef VIRTUOSO_H_ */

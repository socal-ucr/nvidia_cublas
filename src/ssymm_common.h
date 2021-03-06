/*
 * Copyright 1993-2008 NVIDIA Corporation.  All rights reserved.
 *
 * NOTICE TO USER:   
 *
 * This source code is subject to NVIDIA ownership rights under U.S. and
 * international Copyright laws.  
 *
 * This software and the information contained herein is being provided 
 * under the terms and conditions of a Source Code License Agreement.     
 *
 * NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE
 * CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR 
 * IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL,
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS,  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION,  ARISING OUT OF OR IN CONNECTION WITH THE USE
 * OR PERFORMANCE OF THIS SOURCE CODE.  
 *
 * U.S. Government End Users.   This source code is a "commercial item" as 
 * that term is defined at  48 C.F.R. 2.101 (OCT 1995), consisting  of
 * "commercial computer  software"  and "commercial computer software 
 * documentation" as such terms are  used in 48 C.F.R. 12.212 (SEPT 1995)
 * and is provided to the U.S. Government only as a commercial end item.
 * Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through
 * 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the 
 * source code with only those rights set forth herein.
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include "cublas_v1.h"   /* CUBLAS public header file  */
#include "cublasP.h"  /* CUBLAS private header file */

// dimension m, counter i
// dimension n, counter j
// dimension k, counter l

#if (CUBLAS_SSYMM_GRIDW!=CUBLAS_SSYMM_GRIDH)
#error super tile is not square!
#endif

/* Use square 32x32 tiles to access and cache portions of source matrices A,B 
 * and result matrix C
 */
#define TILE_DIM_LOG    (5)
#define TILE_DIM        (1 << TILE_DIM_LOG)
#define TILE_SIZE       (TILE_DIM*TILE_DIM)
#define SUP_TILE_DIM    (TILE_DIM*CUBLAS_SSYMM_GRIDH)

/* In cases where there are more tile elements than threads in a CTA, each
 * thread needs to walk through the tile. To keep the walking pattern simple,
 * we make sure that the number of threads is an integral multiple of the
 * number of elements (i.e. each thread deals with exactly the same number
 * of elements), and that tile dimension (number of rows / number of columns)
 * divides the thread count without remainder. After assigning an initial
 * element to each thread, the thread can then access further elements by 
 * remaining in the same tile row and merely stepping through columns that
 * are COL_INCR apart.
 */
#if ((TILE_SIZE%CUBLAS_SSYMM_THREAD_COUNT)!=0)
#error TILE_SIZE and THREAD_COUNT do not divide evenly!
#endif
#if ((CUBLAS_SSYMM_THREAD_COUNT%TILE_DIM)!=0)
#error THREAD_COUNT and TILE_DIM do not divide evenly!
#endif

#define COL_INCR               (CUBLAS_SSYMM_THREAD_COUNT/TILE_DIM)
#define C_ELEMS_PER_THREAD     (TILE_SIZE/CUBLAS_SSYMM_THREAD_COUNT)
#define A_ELEMS_PER_THREAD     (TILE_SIZE/CUBLAS_SSYMM_THREAD_COUNT)
#define B_ELEMS_PER_THREAD     (TILE_SIZE/CUBLAS_SSYMM_THREAD_COUNT)

__global__ void ssymm_main_hw_lo_right (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_up_right (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_lo_left (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_up_left (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_lo_right (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_up_right (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_lo_left (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_up_left (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_lo_right (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_up_right (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_lo_left (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_up_left (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_lo_right (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_up_right (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_lo_left (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_up_left (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_lo_right_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_up_right_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_lo_left_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_hw_up_left_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_lo_right_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_up_right_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_lo_left_fulltile (struct cublasSsymmParams parms);
__global__ void ssymm_main_sw_up_left_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_lo_right_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_up_right_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_lo_left_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_hw_up_left_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_lo_right_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_up_right_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_lo_left_fulltile (struct cublasSsymmParams parms);
__global__ void fast_ssymm_main_sw_up_left_fulltile (struct cublasSsymmParams parms);

__shared__ float AA[(TILE_DIM+1)*TILE_DIM]; /*pad to elim. GRF bank conflicts*/
__shared__ float BB[(TILE_DIM+1)*TILE_DIM]; /*pad to elim. GRF bank conflicts*/


/* Copyright 2006, 2014, 2021 United States Government as represented
 * by the Administrator of the National Aeronautics and Space
 * Administration. No copyright is claimed in the United States under
 * Title 17, U.S. Code.  All Other Rights Reserved.
 *
 * The refine version 3 unstructured grid adaptation platform is
 * licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * https://www.apache.org/licenses/LICENSE-2.0.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "ref_gather.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ref_edge.h"
#include "ref_egads.h"
#include "ref_endian.h"
#include "ref_export.h"
#include "ref_histogram.h"
#include "ref_malloc.h"
#include "ref_matrix.h"
#include "ref_mpi.h"
#include "ref_sort.h"

REF_FCN REF_STATUS ref_gather_create(REF_GATHER *ref_gather_ptr) {
  REF_GATHER ref_gather;

  ref_malloc(*ref_gather_ptr, 1, REF_GATHER_STRUCT);

  ref_gather = *ref_gather_ptr;

  ref_gather->recording = REF_FALSE;
  ref_gather->grid_file = (FILE *)NULL;
  ref_gather->hist_file = (FILE *)NULL;
  ref_gather->time = 0.0;

  ref_gather->low_quality_zone = REF_FALSE;
  ref_gather->min_quality = 0.1;

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_free(REF_GATHER ref_gather) {
  if (NULL != (void *)(ref_gather->grid_file)) fclose(ref_gather->grid_file);
  if (NULL != (void *)(ref_gather->hist_file)) fclose(ref_gather->hist_file);
  ref_free(ref_gather);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_tec_movie_record_button(REF_GATHER ref_gather,
                                                      REF_BOOL on_or_off) {
  ref_gather->recording = on_or_off;
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_cell_below_quality(
    REF_GRID ref_grid, REF_CELL ref_cell, REF_DBL min_quality,
    REF_GLOB *nnode_global, REF_LONG *ncell_global, REF_GLOB **l2c) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_INT node, part, cell, nodes[REF_CELL_MAX_SIZE_PER];
  REF_INT nnode, ncell;
  REF_INT proc, *counts;
  REF_GLOB offset;
  REF_DBL quality;

  ref_malloc_init(*l2c, ref_node_max(ref_node), REF_GLOB, REF_EMPTY);

  (*nnode_global) = 0;
  (*ncell_global) = 0;
  nnode = 0;
  ncell = 0;

  each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
    RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
    if (ref_mpi_rank(ref_mpi) == part) {
      RSS(ref_node_tet_quality(ref_node, nodes, &quality), "qual");
      if (quality < min_quality) {
        ncell++;
        for (node = 0; node < ref_cell_node_per(ref_cell); node++) {
          if (ref_node_owned(ref_node, nodes[node]) &&
              (REF_EMPTY == (*l2c)[nodes[node]])) {
            (*l2c)[nodes[node]] = nnode;
            nnode++;
          }
        }
      }
    }
  }

  (*ncell_global) = ncell;
  RSS(ref_mpi_allsum(ref_mpi, ncell_global, 1, REF_LONG_TYPE), "allsum");

  ref_malloc(counts, ref_mpi_n(ref_mpi), REF_INT);
  RSS(ref_mpi_allgather(ref_mpi, &nnode, counts, REF_INT_TYPE), "gather size");
  offset = 0;
  for (proc = 0; proc < ref_mpi_rank(ref_mpi); proc++) {
    offset += counts[proc];
  }
  each_ref_mpi_part(ref_mpi, proc) { (*nnode_global) += counts[proc]; }
  ref_free(counts);

  for (node = 0; node < ref_node_max(ref_node); node++) {
    if (REF_EMPTY != (*l2c)[node]) {
      (*l2c)[node] += offset;
    }
  }

  RSS(ref_node_ghost_glob(ref_node, (*l2c), 1), "xfer");

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_tec_part(REF_NODE ref_node,
                                                   REF_GLOB nnode,
                                                   REF_GLOB *l2c, REF_INT ldim,
                                                   REF_DBL *scalar,
                                                   FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB nnode_written, first, global;
  REF_INT local, n, i, id;
  REF_STATUS status;
  REF_INT dim = 3 + ldim + 1;
  REF_INT *sorted_local, *pack, total_cellnode, position;
  REF_GLOB *sorted_cellnode;

  total_cellnode = 0;
  for (i = 0; i < ref_node_max(ref_node); i++) {
    if (REF_EMPTY != l2c[i] && ref_node_owned(ref_node, i)) {
      total_cellnode++;
    }
  }

  ref_malloc(sorted_local, total_cellnode, REF_INT);
  ref_malloc(sorted_cellnode, total_cellnode, REF_GLOB);
  ref_malloc(pack, total_cellnode, REF_INT);

  total_cellnode = 0;
  for (i = 0; i < ref_node_max(ref_node); i++) {
    if (REF_EMPTY != l2c[i] && ref_node_owned(ref_node, i)) {
      sorted_cellnode[total_cellnode] = l2c[i];
      pack[total_cellnode] = i;
      total_cellnode++;
    }
  }
  RSS(ref_sort_heap_glob(total_cellnode, sorted_cellnode, sorted_local),
      "sort");
  for (i = 0; i < total_cellnode; i++) {
    sorted_local[i] = pack[sorted_local[i]];
    sorted_cellnode[i] = l2c[sorted_local[i]];
  }
  ref_free(pack);

  chunk = (REF_INT)(nnode / ref_mpi_n(ref_mpi) + 1);
  chunk = MAX(chunk, 100000);
  chunk = MIN(chunk, ref_mpi_reduce_chunk_limit(
                         ref_mpi, dim * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, dim * chunk, REF_DBL);
  ref_malloc(xyzm, dim * chunk, REF_DBL);

  nnode_written = 0;
  while (nnode_written < nnode) {
    first = nnode_written;
    n = (REF_INT)MIN((REF_GLOB)chunk, nnode - nnode_written);

    nnode_written += n;

    for (i = 0; i < dim * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_sort_search_glob(total_cellnode, sorted_cellnode, global,
                                    &position);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status) {
        local = sorted_local[position];
        local_xyzm[0 + dim * i] = ref_node_xyz(ref_node, 0, local);
        local_xyzm[1 + dim * i] = ref_node_xyz(ref_node, 1, local);
        local_xyzm[2 + dim * i] = ref_node_xyz(ref_node, 2, local);
        for (id = 0; id < ldim; id++) {
          local_xyzm[3 + id + dim * i] = scalar[id + ldim * local];
        }
        local_xyzm[3 + ldim + dim * i] = 1.0;
      }
    }

    for (i = 0; i < n; i++) {
      if ((ABS(local_xyzm[3 + ldim + dim * i] - 1.0) > 0.1) &&
          (ABS(local_xyzm[3 + ldim + dim * i] - 0.0) > 0.1)) {
        printf("%s: %d: %s: before sum " REF_GLOB_FMT " %f\n", __FILE__,
               __LINE__, __func__, first + i, local_xyzm[3 + ldim + dim * i]);
      }
    }

    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, dim * n, REF_DBL_TYPE), "sum");

    if (ref_mpi_once(ref_mpi)) {
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[3 + ldim + dim * i] - 1.0) > 0.1) {
          printf("%s: %d: %s: after sum " REF_GLOB_FMT " %f\n", __FILE__,
                 __LINE__, __func__, first + i, xyzm[3 + ldim + dim * i]);
        }
        for (id = 0; id < 3 + ldim; id++) {
          fprintf(file, " %.15e", xyzm[id + dim * i]);
        }
        fprintf(file, " \n");
      }
    }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);
  ref_free(sorted_cellnode);
  ref_free(sorted_local);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_tec_block(
    REF_NODE ref_node, REF_GLOB nnode, REF_GLOB *l2c, REF_INT ldim,
    REF_DBL *scalar, int dataformat, FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB nnode_written, first, global;
  REF_INT local, n, i, ivar;
  REF_STATUS status;
  REF_INT *sorted_local, *pack, total_cellnode, position;
  REF_GLOB *sorted_cellnode;

  total_cellnode = 0;
  for (i = 0; i < ref_node_max(ref_node); i++) {
    if (REF_EMPTY != l2c[i] && ref_node_owned(ref_node, i)) {
      total_cellnode++;
    }
  }

  ref_malloc(sorted_local, total_cellnode, REF_INT);
  ref_malloc(sorted_cellnode, total_cellnode, REF_GLOB);
  ref_malloc(pack, total_cellnode, REF_INT);

  total_cellnode = 0;
  for (i = 0; i < ref_node_max(ref_node); i++) {
    if (REF_EMPTY != l2c[i] && ref_node_owned(ref_node, i)) {
      sorted_cellnode[total_cellnode] = l2c[i];
      pack[total_cellnode] = i;
      total_cellnode++;
    }
  }
  RSS(ref_sort_heap_glob(total_cellnode, sorted_cellnode, sorted_local),
      "sort");
  for (i = 0; i < total_cellnode; i++) {
    sorted_local[i] = pack[sorted_local[i]];
    sorted_cellnode[i] = l2c[sorted_local[i]];
  }
  ref_free(pack);

  chunk = (REF_INT)(nnode / ref_mpi_n(ref_mpi) + 1);
  chunk = MAX(chunk, 100000);
  chunk =
      MIN(chunk, ref_mpi_reduce_chunk_limit(ref_mpi, (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, chunk, REF_DBL);
  ref_malloc(xyzm, chunk, REF_DBL);

  for (ivar = 0; ivar < 3 + ldim; ivar++) {
    nnode_written = 0;
    while (nnode_written < nnode) {
      first = nnode_written;
      n = (REF_INT)MIN((REF_GLOB)chunk, nnode - nnode_written);

      nnode_written += n;

      for (i = 0; i < chunk; i++) local_xyzm[i] = 0.0;

      for (i = 0; i < n; i++) {
        global = first + i;
        status = ref_sort_search_glob(total_cellnode, sorted_cellnode, global,
                                      &position);
        RXS(status, REF_NOT_FOUND, "node local failed");
        if (REF_SUCCESS == status) {
          local = sorted_local[position];
          if (ivar < 3) {
            local_xyzm[i] = ref_node_xyz(ref_node, ivar, local);
          } else {
            local_xyzm[i] = scalar[(ivar - 3) + ldim * local];
          }
        }
      }

      RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, n, REF_DBL_TYPE), "sum");

      if (ref_mpi_once(ref_mpi)) {
        switch (dataformat) {
          case 1:
            for (i = 0; i < n; i++) {
              float single_float;
              single_float = (float)xyzm[i];
              REIS(1, fwrite(&single_float, sizeof(float), 1, file),
                   "single float");
            }
            break;
          case 2:
            REIS(n, fwrite(xyzm, sizeof(double), (unsigned long)n, file),
                 "block chunk");
            break;
          default:
            return REF_IMPLEMENT;
        }
      }
    }
    REIS(nnode, nnode_written, "node miscount");
  }

  ref_free(xyzm);
  ref_free(local_xyzm);
  ref_free(sorted_cellnode);
  ref_free(sorted_local);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_cell_tec(REF_NODE ref_node,
                                              REF_CELL ref_cell,
                                              REF_LONG ncell_expected,
                                              REF_GLOB *l2c, REF_BOOL binary,
                                              FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT cell, node;
  REF_INT nodes[REF_CELL_MAX_SIZE_PER];
  REF_GLOB globals[REF_CELL_MAX_SIZE_PER];
  REF_INT node_per = ref_cell_node_per(ref_cell);
  REF_GLOB *c2n;
  REF_INT *int_c2n;
  REF_INT proc, part, ncell;
  REF_LONG ncell_actual;

  ncell_actual = 0;

  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "tet cell start");

  if (ref_mpi_once(ref_mpi)) {
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        for (node = 0; node < node_per; node++) {
          globals[node] = l2c[nodes[node]];
        }
        if (binary) {
          for (node = 0; node < node_per; node++) {
            int int_node;
            int_node = (int)globals[node]; /* binary zero-based */
            REIS(1, fwrite(&int_node, sizeof(int), 1, file), "int c2n");
          }
        } else {
          for (node = 0; node < node_per; node++) {
            globals[node]++; /* ascii one-based */
            fprintf(file, " " REF_GLOB_FMT, globals[node]);
          }
          fprintf(file, "\n");
        }
        ncell_actual++;
      }
    }
  }

  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "tet cell master");

  if (ref_mpi_once(ref_mpi)) {
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ncell, 1, REF_INT_TYPE, proc),
          "recv ncell");
      ref_malloc(c2n, ncell * node_per, REF_GLOB);
      ref_malloc(int_c2n, ncell * node_per, REF_INT);
      RSS(ref_mpi_gather_recv(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE,
                              proc),
          "recv c2n");

      if (binary) { /* binary 0-based int, ASCII 1-based */
        for (cell = 0; cell < ncell * node_per; cell++) {
          int_c2n[cell] = (REF_INT)c2n[cell];
        }
        REIS(ncell * node_per,
             fwrite(int_c2n, sizeof(int), (size_t)(ncell * node_per), file),
             "int c2n");
      } else {
        for (cell = 0; cell < ncell * node_per; cell++) {
          c2n[cell]++;
        }
        for (cell = 0; cell < ncell; cell++) {
          for (node = 0; node < node_per; node++) {
            fprintf(file, " " REF_GLOB_FMT, c2n[node + node_per * cell]);
          }
          fprintf(file, "\n");
        }
      }
      ncell_actual += ncell;

      ref_free(int_c2n);
      ref_free(c2n);
    }
  } else {
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) ncell++;
    }
    RSS(ref_mpi_gather_send(ref_mpi, &ncell, 1, REF_INT_TYPE), "send ncell");
    ref_malloc(c2n, ncell * node_per, REF_GLOB);
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        for (node = 0; node < node_per; node++)
          c2n[node + node_per * ncell] = l2c[nodes[node]];
        ncell++;
      }
    }
    RSS(ref_mpi_gather_send(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE),
        "send c2n");

    ref_free(c2n);
  }

  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "tet cell off");

  if (ref_mpi_once(ref_mpi)) {
    REIS(ncell_expected, ncell_actual, "cell count mismatch");
  }

  return REF_SUCCESS;
}

#define TEC_BRICK_TET(brick, nodes) \
  {                                 \
    (brick)[0] = (nodes)[0];        \
    (brick)[1] = (nodes)[1];        \
    (brick)[2] = (nodes)[2];        \
    (brick)[3] = (nodes)[2];        \
    (brick)[4] = (nodes)[3];        \
    (brick)[5] = (nodes)[3];        \
    (brick)[6] = (nodes)[3];        \
    (brick)[7] = (nodes)[3];        \
  }

#define TEC_BRICK_PYR(brick, nodes) \
  {                                 \
    (brick)[0] = (nodes)[0];        \
    (brick)[1] = (nodes)[1];        \
    (brick)[2] = (nodes)[2];        \
    (brick)[3] = (nodes)[3];        \
    (brick)[4] = (nodes)[4];        \
    (brick)[5] = (nodes)[4];        \
    (brick)[6] = (nodes)[4];        \
    (brick)[7] = (nodes)[4];        \
  }

#define TEC_BRICK_PRI(brick, nodes) \
  {                                 \
    (brick)[0] = (nodes)[0];        \
    (brick)[1] = (nodes)[1];        \
    (brick)[2] = (nodes)[2];        \
    (brick)[3] = (nodes)[2];        \
    (brick)[4] = (nodes)[3];        \
    (brick)[5] = (nodes)[4];        \
    (brick)[6] = (nodes)[5];        \
    (brick)[7] = (nodes)[5];        \
  }

#define TEC_BRICK_HEX(brick, nodes) \
  {                                 \
    (brick)[0] = (nodes)[0];        \
    (brick)[1] = (nodes)[1];        \
    (brick)[2] = (nodes)[2];        \
    (brick)[3] = (nodes)[3];        \
    (brick)[4] = (nodes)[4];        \
    (brick)[5] = (nodes)[5];        \
    (brick)[6] = (nodes)[6];        \
    (brick)[7] = (nodes)[7];        \
  }

REF_FCN static REF_STATUS ref_gather_brick_tec(REF_NODE ref_node,
                                               REF_CELL ref_cell,
                                               REF_LONG ncell_expected,
                                               REF_GLOB *l2c, REF_BOOL binary,
                                               FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT cell, node;
  REF_INT nodes[REF_CELL_MAX_SIZE_PER];
  REF_GLOB brick[8];
  REF_GLOB globals[REF_CELL_MAX_SIZE_PER];
  REF_INT node_per = ref_cell_node_per(ref_cell);
  REF_GLOB *c2n;
  REF_INT proc, part, ncell;
  REF_LONG ncell_actual;

  ncell_actual = 0;

  if (ref_mpi_once(ref_mpi)) {
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        for (node = 0; node < node_per; node++) {
          globals[node] = l2c[nodes[node]];
        }
        switch (ref_cell_node_per(ref_cell)) {
          case 4:
            REF_CELL_TEC_BRICK_TET(brick, globals);
            break;
          case 5:
            REF_CELL_TEC_BRICK_PYR(brick, globals);
            break;
          case 6:
            REF_CELL_TEC_BRICK_PRI(brick, globals);
            break;
          case 8:
            REF_CELL_TEC_BRICK_HEX(brick, globals);
            break;
          default:
            RSS(REF_IMPLEMENT, "wrong nodes per cell");
            break;
        }
        if (binary) {
          for (node = 0; node < 8; node++) {
            int int_node;
            int_node = (int)brick[node]; /* binary zero-based */
            REIS(1, fwrite(&int_node, sizeof(int), 1, file), "int c2n");
          }
        } else {
          for (node = 0; node < 8; node++) {
            brick[node]++; /* ascii one-based */
            fprintf(file, " " REF_GLOB_FMT, brick[node]);
          }
          fprintf(file, "\n");
        }
        ncell_actual++;
      }
    }
  }

  if (ref_mpi_once(ref_mpi)) {
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ncell, 1, REF_INT_TYPE, proc),
          "recv ncell");
      ref_malloc(c2n, ncell * node_per, REF_GLOB);
      RSS(ref_mpi_gather_recv(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE,
                              proc),
          "recv c2n");
      for (cell = 0; cell < ncell; cell++) {
        switch (ref_cell_node_per(ref_cell)) {
          case 4:
            REF_CELL_TEC_BRICK_TET(brick, &(c2n[node_per * cell]));
            break;
          case 5:
            REF_CELL_TEC_BRICK_PYR(brick, &(c2n[node_per * cell]));
            break;
          case 6:
            REF_CELL_TEC_BRICK_PRI(brick, &(c2n[node_per * cell]));
            break;
          case 8:
            REF_CELL_TEC_BRICK_HEX(brick, &(c2n[node_per * cell]));
            break;
          default:
            RSS(REF_IMPLEMENT, "wrong nodes per cell");
            break;
        }
        if (binary) {
          for (node = 0; node < 8; node++) {
            int int_node;
            int_node = (int)brick[node]; /* binary zero-based */
            REIS(1, fwrite(&int_node, sizeof(int), 1, file), "int c2n");
          }
        } else {
          for (node = 0; node < 8; node++) {
            brick[node]++; /* ascii one-based */
            fprintf(file, " " REF_GLOB_FMT, brick[node]);
          }
          fprintf(file, "\n");
        }
        ncell_actual++;
      }
      ref_free(c2n);
    }
  } else {
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) ncell++;
    }
    RSS(ref_mpi_gather_send(ref_mpi, &ncell, 1, REF_INT_TYPE), "send ncell");
    ref_malloc(c2n, ncell * node_per, REF_GLOB);
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        for (node = 0; node < node_per; node++)
          c2n[node + node_per * ncell] = l2c[nodes[node]];
        ncell++;
      }
    }
    RSS(ref_mpi_gather_send(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE),
        "send c2n");

    ref_free(c2n);
  }

  if (ref_mpi_once(ref_mpi)) {
    REIS(ncell_expected, ncell_actual, "cell count mismatch");
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_cell_id_tec(
    REF_NODE ref_node, REF_CELL ref_cell, REF_INT cell_id,
    REF_LONG ncell_expected, REF_GLOB *l2c, REF_BOOL binary, FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT cell, node;
  REF_INT nodes[REF_CELL_MAX_SIZE_PER];
  REF_GLOB globals[REF_CELL_MAX_SIZE_PER];
  REF_INT node_per = ref_cell_node_per(ref_cell);
  REF_GLOB *c2n;
  REF_INT *int_c2n;
  REF_INT proc, part, ncell;
  REF_LONG ncell_actual;

  ncell_actual = 0;

  if (ref_mpi_once(ref_mpi)) {
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      if (cell_id == nodes[ref_cell_id_index(ref_cell)]) {
        RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
        if (ref_mpi_rank(ref_mpi) == part) {
          for (node = 0; node < node_per; node++) {
            globals[node] = l2c[nodes[node]];
          }
          if (binary) {
            for (node = 0; node < node_per; node++) {
              int int_node;
              int_node = (int)globals[node]; /* binary zero-based */
              REIS(1, fwrite(&int_node, sizeof(int), 1, file), "int c2n");
            }
          } else {
            for (node = 0; node < node_per; node++) {
              globals[node]++; /* ascii one-based */
              fprintf(file, " " REF_GLOB_FMT, globals[node]);
            }
            fprintf(file, "\n");
          }
          ncell_actual++;
        }
      }
    }
  }

  if (ref_mpi_once(ref_mpi)) {
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ncell, 1, REF_INT_TYPE, proc),
          "recv ncell");
      ref_malloc(c2n, ncell * node_per, REF_GLOB);
      ref_malloc(int_c2n, ncell * node_per, REF_INT);
      RSS(ref_mpi_gather_recv(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE,
                              proc),
          "recv c2n");

      if (binary) { /* binary 0-based int, ASCII 1-based */
        for (cell = 0; cell < ncell * node_per; cell++) {
          int_c2n[cell] = (REF_INT)c2n[cell];
        }
        REIS(ncell * node_per,
             fwrite(int_c2n, sizeof(int), (size_t)(ncell * node_per), file),
             "int c2n");
      } else {
        for (cell = 0; cell < ncell * node_per; cell++) {
          c2n[cell]++;
        }
        for (cell = 0; cell < ncell; cell++) {
          for (node = 0; node < node_per; node++) {
            fprintf(file, " " REF_GLOB_FMT, c2n[node + node_per * cell]);
          }
          fprintf(file, "\n");
        }
      }
      ncell_actual += ncell;

      ref_free(int_c2n);
      ref_free(c2n);
    }
  } else {
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      if (cell_id == nodes[ref_cell_id_index(ref_cell)]) {
        RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
        if (ref_mpi_rank(ref_mpi) == part) ncell++;
      }
    }
    RSS(ref_mpi_gather_send(ref_mpi, &ncell, 1, REF_INT_TYPE), "send ncell");
    ref_malloc(c2n, ncell * node_per, REF_GLOB);
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      if (cell_id == nodes[ref_cell_id_index(ref_cell)]) {
        RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
        if (ref_mpi_rank(ref_mpi) == part) {
          for (node = 0; node < node_per; node++)
            c2n[node + node_per * ncell] = l2c[nodes[node]];
          ncell++;
        }
      }
    }
    RSS(ref_mpi_gather_send(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE),
        "send c2n");

    ref_free(c2n);
  }

  if (ref_mpi_once(ref_mpi)) {
    REIS(ncell_expected, ncell_actual, "cell count mismatch");
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_cell_quality_tec(
    REF_NODE ref_node, REF_CELL ref_cell, REF_LONG ncell_expected,
    REF_GLOB *l2c, REF_DBL min_quality, FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT cell, node;
  REF_INT nodes[REF_CELL_MAX_SIZE_PER];
  REF_GLOB globals[REF_CELL_MAX_SIZE_PER];
  REF_INT node_per = ref_cell_node_per(ref_cell);
  REF_INT ncell;
  REF_GLOB *c2n;
  REF_INT part, proc;
  REF_LONG ncell_actual;
  REF_DBL quality;

  ncell_actual = 0;

  if (ref_mpi_once(ref_mpi)) {
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        RSS(ref_node_tet_quality(ref_node, nodes, &quality), "qual");
        if (quality < min_quality) {
          for (node = 0; node < node_per; node++) {
            globals[node] = l2c[nodes[node]];
            globals[node]++;
            fprintf(file, " " REF_GLOB_FMT, globals[node]);
          }
          ncell_actual++;
          fprintf(file, "\n");
        }
      }
    }
  }

  if (ref_mpi_once(ref_mpi)) {
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ncell, 1, REF_INT_TYPE, proc),
          "recv ncell");
      ref_malloc(c2n, ncell * node_per, REF_GLOB);
      RSS(ref_mpi_gather_recv(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE,
                              proc),
          "recv c2n");
      for (cell = 0; cell < ncell; cell++) {
        for (node = 0; node < node_per; node++) {
          c2n[node + node_per * cell]++;
          fprintf(file, " " REF_GLOB_FMT, c2n[node + node_per * cell]);
        }
        fprintf(file, "\n");
      }
      ncell_actual++;
      ref_free(c2n);
    }
  } else {
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        RSS(ref_node_tet_quality(ref_node, nodes, &quality), "qual");
        if (quality < min_quality) ncell++;
      }
    }
    RSS(ref_mpi_gather_send(ref_mpi, &ncell, 1, REF_INT_TYPE), "send ncell");
    ref_malloc(c2n, ncell * node_per, REF_GLOB);
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        RSS(ref_node_tet_quality(ref_node, nodes, &quality), "qual");
        if (quality < min_quality) {
          for (node = 0; node < node_per; node++)
            c2n[node + node_per * ncell] = l2c[nodes[node]];
          ncell++;
        }
      }
    }
    RSS(ref_mpi_gather_send(ref_mpi, c2n, ncell * node_per, REF_GLOB_TYPE),
        "send c2n");
    ref_free(c2n);
  }

  if (ref_mpi_once(ref_mpi)) {
    REIS(ncell_expected, ncell_actual, "cell count mismatch");
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_tec_histogram_frame(
    REF_GRID ref_grid, const char *zone_title) {
  REF_GATHER ref_gather = ref_grid_gather(ref_grid);
  REF_HISTOGRAM ref_histogram;

  if (ref_grid_once(ref_grid)) {
    if (NULL == (void *)(ref_gather->hist_file)) {
      ref_gather->hist_file = fopen("ref_gather_histo.tec", "w");
      if (NULL == (void *)(ref_gather->hist_file))
        printf("unable to open ref_gather_histo.tec\n");
      RNS(ref_gather->hist_file, "unable to open file");

      fprintf(ref_gather->hist_file, "title=\"tecplot refine histogram\"\n");
      fprintf(ref_gather->hist_file,
              "variables = \"Edge Length\" \"Normalized Count\"\n");
    }
  }

  RSS(ref_histogram_create(&ref_histogram), "create");
  RSS(ref_histogram_resolution(ref_histogram, 288, 12.0), "res");

  RSS(ref_histogram_add_ratio(ref_histogram, ref_grid), "add ratio");

  if (ref_grid_once(ref_grid)) {
    RSS(ref_histogram_zone(ref_histogram, ref_gather->hist_file, zone_title,
                           ref_gather->time),
        "tec zone");
  }

  RSS(ref_histogram_free(ref_histogram), "free gram");

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_tec_movie_frame(REF_GRID ref_grid,
                                              const char *zone_title) {
  REF_GATHER ref_gather = ref_grid_gather(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell = ref_grid_tri(ref_grid);
  REF_GEOM ref_geom = ref_grid_geom(ref_grid);
  REF_INT cell, cell_node, nodes[REF_CELL_MAX_SIZE_PER];
  REF_INT node;
  REF_GLOB nnode, *l2c;
  REF_LONG ncell;
  REF_DBL *scalar, dot, quality;
  REF_INT edge, node0, node1;
  REF_EDGE ref_edge;
  REF_DBL edge_ratio;
  REF_INT ldim = 4;
  REF_BOOL metric_area = REF_FALSE;

  if (!(ref_gather->recording)) return REF_SUCCESS;

  RSS(ref_gather_tec_histogram_frame(ref_grid, zone_title), "hist frame");

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");

  if (ref_grid_once(ref_grid)) {
    if (NULL == (void *)(ref_gather->grid_file)) {
      ref_gather->grid_file = fopen("ref_gather_movie.tec", "w");
      if (NULL == (void *)(ref_gather->grid_file))
        printf("unable to open ref_gather_movie.tec\n");
      RNS(ref_gather->grid_file, "unable to open file");

      fprintf(ref_gather->grid_file,
              "title=\"tecplot refine partition file\"\n");
      fprintf(ref_gather->grid_file,
              "variables = \"x\" \"y\" \"z\" \"n\" \"s\" \"l\" \"p\"\n");
    }
    if (NULL == zone_title) {
      fprintf(ref_gather->grid_file,
              "zone t=\"surf\", nodes=" REF_GLOB_FMT
              ", elements=%ld, datapacking=%s, "
              "zonetype=%s, solutiontime=%f\n",
              nnode, ncell, "point", "fetriangle", ref_gather->time);
    } else {
      fprintf(ref_gather->grid_file,
              "zone t=\"%s\", nodes=" REF_GLOB_FMT
              ", elements=%ld, datapacking=%s, "
              "zonetype=%s, solutiontime=%f\n",
              zone_title, nnode, ncell, "point", "fetriangle",
              ref_gather->time);
    }
  }

  ref_malloc(scalar, ldim * ref_node_max(ref_node), REF_DBL);
  each_ref_node_valid_node(ref_node, node) {
    scalar[0 + ldim * node] = 2.0;
    scalar[1 + ldim * node] = 1.0;
    scalar[2 + ldim * node] = 1.0;
    scalar[3 + ldim * node] = (REF_DBL)ref_node_part(ref_node, node);
  }
  if (ref_geom_model_loaded(ref_geom) || ref_grid_twod(ref_grid)) {
    if (ref_geom_model_loaded(ref_geom) || ref_geom_meshlinked(ref_geom)) {
      each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
        RSS(ref_geom_tri_norm_deviation(ref_grid, nodes, &dot), "norm dev");
        each_ref_cell_cell_node(ref_cell, cell_node) {
          scalar[0 + ldim * nodes[cell_node]] =
              MIN(scalar[0 + ldim * nodes[cell_node]], dot);
        }
      }
    }
    if (ref_grid_twod(ref_grid)) {
      each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
        RSS(ref_node_tri_quality(ref_node, nodes, &quality), "tri qual");
        each_ref_cell_cell_node(ref_cell, cell_node) {
          scalar[0 + ldim * nodes[cell_node]] =
              MIN(scalar[0 + ldim * nodes[cell_node]], quality);
        }
      }
    }
    RSS(ref_edge_create(&ref_edge, ref_grid), "create edges");
    for (edge = 0; edge < ref_edge_n(ref_edge); edge++) {
      node0 = ref_edge_e2n(ref_edge, 0, edge);
      node1 = ref_edge_e2n(ref_edge, 1, edge);
      RSS(ref_node_ratio(ref_node, node0, node1, &edge_ratio), "ratio");
      scalar[1 + ldim * node0] = MIN(scalar[1 + ldim * node0], edge_ratio);
      scalar[1 + ldim * node1] = MIN(scalar[1 + ldim * node1], edge_ratio);
      scalar[2 + ldim * node0] = MAX(scalar[2 + ldim * node0], edge_ratio);
      scalar[2 + ldim * node1] = MAX(scalar[2 + ldim * node1], edge_ratio);
    }
    RSS(ref_edge_free(ref_edge), "free edges");
  }

  if (metric_area && ref_grid_twod(ref_grid)) {
    REF_DBL area;
    REF_INT i, *hits;
    ref_malloc_init(hits, ref_node_max(ref_node), REF_INT, 0);
    each_ref_node_valid_node(ref_node, node) { scalar[3 + ldim * node] = 0.0; }
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_node_tri_metric_area(ref_node, nodes, &area), "tri area");
      for (i = 0; i < 3; i++) {
        scalar[3 + ldim * nodes[i]] += area;
        (hits[nodes[i]])++;
      }
    }
    each_ref_node_valid_node(ref_node, node) {
      if (hits[node] > 0) {
        scalar[3 + ldim * node] /= ((REF_DBL)hits[node]);
      }
    }
    ref_free(hits);
  }

  RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar,
                               ref_gather->grid_file),
      "nodes");
  RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_FALSE,
                          ref_gather->grid_file),
      "t");
  ref_free(l2c);

  if (ref_gather_low_quality_zone(ref_gather)) {
    RSS(ref_gather_cell_below_quality(ref_grid, ref_grid_tet(ref_grid),
                                      ref_gather->min_quality, &nnode, &ncell,
                                      &l2c),
        "cell below");

    if (node > 0 && ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        if (NULL == zone_title) {
          fprintf(ref_gather->grid_file,
                  "zone t=\"qtet\", nodes=" REF_GLOB_FMT
                  ", elements=%ld, datapacking=%s, "
                  "zonetype=%s, solutiontime=%f\n",
                  nnode, ncell, "point", "fetetrahedron", ref_gather->time);
        } else {
          fprintf(ref_gather->grid_file,
                  "zone t=\"q%s\", nodes=" REF_GLOB_FMT
                  ", elements=%ld, datapacking=%s, "
                  "zonetype=%s, solutiontime=%f\n",
                  zone_title, nnode, ncell, "point", "fetetrahedron",
                  ref_gather->time);
        }
      }
      RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar,
                                   ref_gather->grid_file),
          "nodes");
      RSS(ref_gather_cell_quality_tec(ref_node, ref_grid_tet(ref_grid), ncell,
                                      l2c, ref_gather->min_quality,
                                      ref_gather->grid_file),
          "qtet");
    }
    ref_free(l2c);
  }

  if (ref_grid_once(ref_grid)) {
    REIS(0, fflush(ref_gather->grid_file), "gather movie fflush");
    (ref_gather->time) += 1.0;
  }

  ref_free(scalar);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_tec_part(REF_GRID ref_grid,
                                       const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell = ref_grid_tri(ref_grid);
  REF_INT node;
  REF_GLOB nnode, *l2c;
  REF_LONG ncell;
  REF_DBL *scalar;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    fprintf(file, "title=\"tecplot refine partition file\"\n");
    fprintf(file, "variables = \"x\" \"y\" \"z\" \"p\" \"a\"\n");
    fprintf(file,
            "zone t=\"surf\", nodes=" REF_GLOB_FMT
            ", elements=%ld, datapacking=%s, "
            "zonetype=%s\n",
            nnode, ncell, "point", "fetriangle");
  }

  ref_malloc(scalar, 2 * ref_node_max(ref_node), REF_DBL);
  each_ref_node_valid_node(ref_node, node) {
    scalar[0 + 2 * node] = (REF_DBL)ref_node_part(ref_node, node);
    scalar[1 + 2 * node] = (REF_DBL)ref_node_age(ref_node, node);
  }

  RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, 2, scalar, file), "nodes");
  RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_FALSE, file),
      "nodes");

  if (ref_grid_once(ref_grid)) fclose(file);

  ref_free(scalar);
  ref_free(l2c);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_tec(REF_GRID ref_grid,
                                         const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell;
  REF_GLOB nnode, *l2c;
  REF_LONG ncell;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    fprintf(file, "title=\"geometry\"\n");
    fprintf(file, "variables = \"x\" \"y\" \"z\"\n");
  }

  ref_cell = ref_grid_edg(ref_grid);
  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode > 0 && ncell > 0) {
    if (ref_grid_once(ref_grid)) {
      fprintf(file,
              "zone t=\"edge\", nodes=" REF_GLOB_FMT
              ", elements=%ld, datapacking=%s, "
              "zonetype=%s\n",
              nnode, ncell, "point", "felineseg");
    }
    RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, 0, NULL, file), "nodes");
    RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_FALSE, file),
        "nodes");
  }
  ref_free(l2c);

  ref_cell = ref_grid_tri(ref_grid);
  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode > 0 && ncell > 0) {
    if (ref_grid_once(ref_grid)) {
      fprintf(file,
              "zone t=\"face\", nodes=" REF_GLOB_FMT
              ", elements=%ld, datapacking=%s, "
              "zonetype=%s\n",
              nnode, ncell, "point", "fetriangle");
    }
    RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, 0, NULL, file), "nodes");
    RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_FALSE, file),
        "nodes");
  }
  ref_free(l2c);

  ref_cell = ref_grid_tet(ref_grid);
  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode > 0 && ncell > 0) {
    if (ref_grid_once(ref_grid)) {
      fprintf(file,
              "zone t=\"tet\", nodes=" REF_GLOB_FMT
              ", elements=%ld, datapacking=%s, "
              "zonetype=%s\n",
              nnode, ncell, "point", "fetetrahedron");
    }
    RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, 0, NULL, file), "nodes");
    RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_FALSE, file),
        "nodes");
  }
  ref_free(l2c);

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_meshb_size(FILE *file, REF_INT version,
                                                REF_SIZE value) {
  unsigned int int_value;
  unsigned long long_value;
  if (version < 4) {
    int_value = (unsigned int)value;
    REIS(1, fwrite(&int_value, sizeof(unsigned int), 1, file), "int value");
  } else {
    long_value = value;
    REIS(1, fwrite(&long_value, sizeof(unsigned long), 1, file), "long value");
  }
  return REF_SUCCESS;
}
REF_FCN static REF_STATUS ref_gather_meshb_glob(FILE *file, REF_INT version,
                                                REF_GLOB value) {
  int int_value;
  long long_value;
  if (version < 4) {
    int_value = (int)value;
    REIS(1, fwrite(&int_value, sizeof(int), 1, file), "int value");
  } else {
    long_value = (long)value;
    REIS(1, fwrite(&long_value, sizeof(long), 1, file), "long value");
  }
  return REF_SUCCESS;
}
REF_FCN static REF_STATUS ref_gather_meshb_int(FILE *file, REF_INT version,
                                               REF_INT value) {
  int int_value;
  long long_value;
  if (version < 4) {
    int_value = (int)value;
    REIS(1, fwrite(&int_value, sizeof(int), 1, file), "int value");
  } else {
    long_value = (long)value;
    REIS(1, fwrite(&long_value, sizeof(long), 1, file), "long value");
  }
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node(REF_NODE ref_node,
                                          REF_BOOL swap_endian, REF_INT version,
                                          REF_BOOL twod, FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_DBL swapped_dbl;
  REF_GLOB nnode_written, first, global;
  REF_INT n, i;
  REF_INT local;
  REF_STATUS status;
  REF_BOOL node_not_used_once = REF_FALSE;

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk = MIN(
      chunk, ref_mpi_reduce_chunk_limit(ref_mpi, 4 * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, 4 * chunk, REF_DBL);
  ref_malloc(xyzm, 4 * chunk, REF_DBL);

  nnode_written = 0;
  while (nnode_written < ref_node_n_global(ref_node)) {
    first = nnode_written;
    n = (REF_INT)MIN((REF_GLOB)chunk,
                     ref_node_n_global(ref_node) - nnode_written);

    nnode_written += n;

    for (i = 0; i < 4 * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_node_local(ref_node, global, &local);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status &&
          ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
        local_xyzm[0 + 4 * i] = ref_node_xyz(ref_node, 0, local);
        local_xyzm[1 + 4 * i] = ref_node_xyz(ref_node, 1, local);
        local_xyzm[2 + 4 * i] = ref_node_xyz(ref_node, 2, local);
        local_xyzm[3 + 4 * i] = 1.0;
      } else {
        local_xyzm[0 + 4 * i] = 0.0;
        local_xyzm[1 + 4 * i] = 0.0;
        local_xyzm[2 + 4 * i] = 0.0;
        local_xyzm[3 + 4 * i] = 0.0;
      }
    }

    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, 4 * n, REF_DBL_TYPE), "sum");

    if (ref_mpi_once(ref_mpi))
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[3 + 4 * i] - 1.0) > 0.1) {
          printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                 xyzm[3 + 4 * i]);
          node_not_used_once = REF_TRUE;
        }
        swapped_dbl = xyzm[0 + 4 * i];
        if (swap_endian) SWAP_DBL(swapped_dbl);
        REIS(1, fwrite(&swapped_dbl, sizeof(REF_DBL), 1, file), "x");
        swapped_dbl = xyzm[1 + 4 * i];
        if (swap_endian) SWAP_DBL(swapped_dbl);
        REIS(1, fwrite(&swapped_dbl, sizeof(REF_DBL), 1, file), "y");
        if (!twod) {
          swapped_dbl = xyzm[2 + 4 * i];
          if (swap_endian) SWAP_DBL(swapped_dbl);
          REIS(1, fwrite(&swapped_dbl, sizeof(REF_DBL), 1, file), "z");
        }
        if (1 <= version && version <= 4)
          RSS(ref_gather_meshb_int(file, version, REF_EXPORT_MESHB_VERTEX_ID),
              "nnode");
      }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  RSS(ref_mpi_all_or(ref_mpi, &node_not_used_once), "all gather error code");
  RAS(!node_not_used_once, "node used more or less than once");

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_metric(REF_NODE ref_node,
                                                 FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB global, nnode_written, first;
  REF_INT local, n, i, im;
  REF_STATUS status;

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk = MIN(
      chunk, ref_mpi_reduce_chunk_limit(ref_mpi, 7 * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, 7 * chunk, REF_DBL);
  ref_malloc(xyzm, 7 * chunk, REF_DBL);

  nnode_written = 0;
  while (nnode_written < ref_node_n_global(ref_node)) {
    first = nnode_written;
    n = (REF_INT)MIN((REF_GLOB)chunk,
                     ref_node_n_global(ref_node) - nnode_written);

    nnode_written += n;

    for (i = 0; i < 7 * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_node_local(ref_node, global, &local);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status &&
          ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
        RSS(ref_node_metric_get(ref_node, local, &(local_xyzm[7 * i])), "get");
        local_xyzm[6 + 7 * i] = 1.0;
      } else {
        for (im = 0; im < 7; im++) local_xyzm[im + 7 * i] = 0.0;
      }
    }

    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, 7 * n, REF_DBL_TYPE), "sum");

    if (ref_mpi_once(ref_mpi))
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[6 + 7 * i] - 1.0) > 0.1) {
          printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                 xyzm[6 + 7 * i]);
        }
        fprintf(file, "%.15e %.15e %.15e %.15e %.15e %.15e \n", xyzm[0 + 7 * i],
                xyzm[1 + 7 * i], xyzm[2 + 7 * i], xyzm[3 + 7 * i],
                xyzm[4 + 7 * i], xyzm[5 + 7 * i]);
      }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_bamg_met(REF_GRID ref_grid,
                                                   FILE *file) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB global, nnode_written, first;
  REF_INT local, n, i, im;
  REF_STATUS status;

  RAS(ref_grid_twod(ref_grid), "only implemented for twod mesh");

  if (ref_mpi_once(ref_mpi)) {
    fprintf(file, "%ld %d\n", (long)ref_node_n_global(ref_node), 3);
  }

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk = MIN(
      chunk, ref_mpi_reduce_chunk_limit(ref_mpi, 7 * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, 7 * chunk, REF_DBL);
  ref_malloc(xyzm, 7 * chunk, REF_DBL);

  nnode_written = 0;
  while (nnode_written < ref_node_n_global(ref_node)) {
    first = nnode_written;
    n = (REF_INT)MIN((REF_GLOB)chunk,
                     ref_node_n_global(ref_node) - nnode_written);

    nnode_written += n;

    for (i = 0; i < 7 * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_node_local(ref_node, global, &local);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status &&
          ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
        RSS(ref_node_metric_get(ref_node, local, &(local_xyzm[7 * i])), "get");
        local_xyzm[6 + 7 * i] = 1.0;
      } else {
        for (im = 0; im < 7; im++) local_xyzm[im + 7 * i] = 0.0;
      }
    }

    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, 7 * n, REF_DBL_TYPE), "sum");

    if (ref_mpi_once(ref_mpi))
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[6 + 7 * i] - 1.0) > 0.1) {
          printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                 xyzm[6 + 7 * i]);
        }
        fprintf(file, "%.15e %.15e %.15e\n", xyzm[0 + 7 * i], xyzm[1 + 7 * i],
                xyzm[3 + 7 * i]);
      }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_metric_solb(REF_GRID ref_grid,
                                                      FILE *file) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB global, nnode_written, first;
  REF_INT local, n, i, im;
  REF_STATUS status;
  REF_FILEPOS next_position = 0;
  REF_INT keyword_code, header_size;
  REF_INT code, version, dim, nmetric;
  REF_INT int_size, fp_size;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  dim = 3;
  nmetric = 6;
  if (ref_grid_twod(ref_grid)) {
    dim = 2;
    nmetric = 3;
  }

  version = 2;
  if (1 < ref_grid_meshb_version(ref_grid)) {
    version = ref_grid_meshb_version(ref_grid);
  } else {
    if (REF_EXPORT_MESHB_VERTEX_3 < ref_node_n_global(ref_node)) version = 3;
    if (REF_EXPORT_MESHB_VERTEX_4 < ref_node_n_global(ref_node)) version = 4;
  }

  int_size = 4;
  fp_size = 4;
  if (2 < version) fp_size = 8;
  if (3 < version) int_size = 8;
  header_size = 4 + fp_size + int_size;

  if (ref_mpi_once(ref_mpi)) {
    code = 1;
    REIS(1, fwrite(&code, sizeof(int), 1, file), "code");
    REIS(1, fwrite(&version, sizeof(int), 1, file), "version");
    next_position = (REF_FILEPOS)(4 + fp_size + 4) + ftell(file);
    keyword_code = 3;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "dim code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    REIS(1, fwrite(&dim, sizeof(int), 1, file), "dim");
    REIS(next_position, ftell(file), "dim inconsistent");
  }

  if (ref_mpi_once(ref_mpi)) {
    next_position =
        (REF_FILEPOS)header_size + (REF_FILEPOS)(4 + 4) +
        (REF_FILEPOS)ref_node_n_global(ref_node) * (REF_FILEPOS)(nmetric * 8) +
        ftell(file);
    keyword_code = 62;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "vertex version code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    RSS(ref_gather_meshb_glob(file, version, ref_node_n_global(ref_node)),
        "nnode");
    keyword_code = 1; /* one solution at node */
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "n solutions");
    keyword_code = 3; /* solution type 3, metric */
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "metric solution");
  }

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk = MIN(
      chunk, ref_mpi_reduce_chunk_limit(ref_mpi, 7 * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, 7 * chunk, REF_DBL);
  ref_malloc(xyzm, 7 * chunk, REF_DBL);

  nnode_written = 0;
  while (nnode_written < ref_node_n_global(ref_node)) {
    first = nnode_written;

    n = (REF_INT)MIN((REF_GLOB)chunk,
                     ref_node_n_global(ref_node) - nnode_written);

    nnode_written += n;

    for (i = 0; i < 7 * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_node_local(ref_node, global, &local);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status &&
          ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
        RSS(ref_node_metric_get(ref_node, local, &(local_xyzm[7 * i])), "get");
        local_xyzm[6 + 7 * i] = 1.0;
      } else {
        for (im = 0; im < 7; im++) local_xyzm[im + 7 * i] = 0.0;
      }
    }

    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, 7 * n, REF_DBL_TYPE), "sum");

    if (ref_mpi_once(ref_mpi))
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[6 + 7 * i] - 1.0) > 0.1) {
          printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                 xyzm[6 + 7 * i]);
        }
        if (3 == dim) { /* threed */
          REIS(1, fwrite(&(xyzm[0 + 7 * i]), sizeof(REF_DBL), 1, file), "m11");
          REIS(1, fwrite(&(xyzm[1 + 7 * i]), sizeof(REF_DBL), 1, file), "m12");
          /* transposed 3,2 */
          REIS(1, fwrite(&(xyzm[3 + 7 * i]), sizeof(REF_DBL), 1, file), "m22");
          REIS(1, fwrite(&(xyzm[2 + 7 * i]), sizeof(REF_DBL), 1, file), "m13");
          REIS(1, fwrite(&(xyzm[4 + 7 * i]), sizeof(REF_DBL), 1, file), "m23");
          REIS(1, fwrite(&(xyzm[5 + 7 * i]), sizeof(REF_DBL), 1, file), "m33");
        } else { /* twod */
          REIS(1, fwrite(&(xyzm[0 + 7 * i]), sizeof(REF_DBL), 1, file), "m11");
          REIS(1, fwrite(&(xyzm[1 + 7 * i]), sizeof(REF_DBL), 1, file), "m12");
          REIS(1, fwrite(&(xyzm[3 + 7 * i]), sizeof(REF_DBL), 1, file), "m22");
        }
      }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  if (ref_mpi_once(ref_mpi))
    REIS(next_position, ftell(file), "solb metric record len inconsistent");

  if (ref_mpi_once(ref_mpi)) { /* End */
    keyword_code = 54;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "end kw");
    next_position = 0;
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_rst(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char *filename) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB global, nnode_written, first;
  REF_INT local, n, i, im;
  REF_STATUS status;
  FILE *file;
  int variables, step, steps, dof;

  RSS(ref_node_synchronize_globals(ref_node), "sync");
  steps = 2;
  variables = ldim / steps;
  REIS(ldim, variables * steps, "ldim not divisble by steps");
  dof = (int)ref_node_n_global(ref_node);

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    int length = 8;
    char magic[] = "COFFERST";
    int version = 2;
    int dim;
    int doubles = 0;

    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    REIS(1, fwrite(&length, sizeof(length), 1, file), "length");
    REIS(length, fwrite(magic, sizeof(char), (unsigned long)length, file),
         "magic");
    REIS(1, fwrite(&version, sizeof(version), 1, file), "version");
    dim = 3;
    if (ref_grid_twod(ref_grid)) dim = 2;
    REIS(1, fwrite(&dim, sizeof(dim), 1, file), "dim");
    REIS(1, fwrite(&variables, sizeof(variables), 1, file), "variables");
    REIS(1, fwrite(&steps, sizeof(steps), 1, file), "steps");
    REIS(1, fwrite(&dof, sizeof(dof), 1, file), "dof");
    REIS(1, fwrite(&doubles, sizeof(doubles), 1, file), "doubles");
    /* assume zero doubles, skip misc metadata (timestep) */
  }

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk = MIN(chunk, ref_mpi_reduce_chunk_limit(
                         ref_mpi, (variables + 1) * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, (variables + 1) * chunk, REF_DBL);
  ref_malloc(xyzm, (variables + 1) * chunk, REF_DBL);

  for (step = 0; step < steps; step++) {
    nnode_written = 0;
    while (nnode_written < ref_node_n_global(ref_node)) {
      first = nnode_written;
      n = (REF_INT)MIN((REF_GLOB)chunk,
                       ref_node_n_global(ref_node) - nnode_written);

      nnode_written += n;

      for (i = 0; i < (variables + 1) * chunk; i++) local_xyzm[i] = 0.0;

      for (i = 0; i < n; i++) {
        global = first + i;
        status = ref_node_local(ref_node, global, &local);
        RXS(status, REF_NOT_FOUND, "node local failed");
        if (REF_SUCCESS == status &&
            ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
          for (im = 0; im < variables; im++)
            local_xyzm[im + (variables + 1) * i] = scalar[im + ldim * local];
          local_xyzm[variables + (variables + 1) * i] = 1.0;
        } else {
          for (im = 0; im < (variables + 1); im++)
            local_xyzm[im + (variables + 1) * i] = 0.0;
        }
      }

      RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, (variables + 1) * n,
                      REF_DBL_TYPE),
          "sum");

      if (ref_mpi_once(ref_mpi))
        for (i = 0; i < n; i++) {
          if (ABS(xyzm[variables + (variables + 1) * i] - 1.0) > 0.1) {
            printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                   xyzm[variables + (variables + 1) * i]);
          }
          for (im = 0; im < variables; im++) {
            REIS(1,
                 fwrite(&(xyzm[im + (variables + 1) * i]), sizeof(REF_DBL), 1,
                        file),
                 "s");
          }
        }
    }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_scalar_bin(REF_NODE ref_node,
                                                     REF_INT ldim,
                                                     REF_DBL *scalar,
                                                     FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT chunk, nchunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB global, nnode_written, first;
  REF_INT local, n, i, im;
  REF_STATUS status;
  clock_t tic = 0;
  clock_t local_toc = 0;
  clock_t mpi_toc = 0;
  clock_t disk_toc = 0;

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk = MIN(chunk, ref_mpi_reduce_chunk_limit(
                         ref_mpi, (ldim + 1) * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, (ldim + 1) * chunk, REF_DBL);
  ref_malloc(xyzm, (ldim + 1) * chunk, REF_DBL);

  nchunk = 0;
  nnode_written = 0;
  while (nnode_written < ref_node_n_global(ref_node)) {
    nchunk++;
    first = nnode_written;
    n = (REF_INT)MIN((REF_GLOB)chunk,
                     ref_node_n_global(ref_node) - nnode_written);

    nnode_written += n;
    if (1 < ref_mpi_timing(ref_mpi)) tic = clock();
    for (i = 0; i < (ldim + 1) * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_node_local(ref_node, global, &local);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status &&
          ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
        for (im = 0; im < ldim; im++)
          local_xyzm[im + (ldim + 1) * i] = scalar[im + ldim * local];
        local_xyzm[ldim + (ldim + 1) * i] = 1.0;
      } else {
        for (im = 0; im < (ldim + 1); im++)
          local_xyzm[im + (ldim + 1) * i] = 0.0;
      }
    }
    if (1 < ref_mpi_timing(ref_mpi)) local_toc += (clock() - tic);

    if (1 < ref_mpi_timing(ref_mpi)) tic = clock();
    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, (ldim + 1) * n, REF_DBL_TYPE),
        "sum");
    if (1 < ref_mpi_timing(ref_mpi)) mpi_toc += (clock() - tic);

    if (1 < ref_mpi_timing(ref_mpi)) tic = clock();
    if (ref_mpi_once(ref_mpi))
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[ldim + (ldim + 1) * i] - 1.0) > 0.1) {
          printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                 xyzm[ldim + (ldim + 1) * i]);
        }
        for (im = 0; im < ldim; im++) {
          REIS(1,
               fwrite(&(xyzm[im + (ldim + 1) * i]), sizeof(REF_DBL), 1, file),
               "s");
        }
      }
    if (1 < ref_mpi_timing(ref_mpi)) disk_toc += (clock() - tic);
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  if (1 < ref_mpi_timing(ref_mpi)) {
    printf(" local %f mpi %f disk %f rank %d\n",
           ((REF_DBL)local_toc) / ((REF_DBL)CLOCKS_PER_SEC),
           ((REF_DBL)mpi_toc) / ((REF_DBL)CLOCKS_PER_SEC),
           ((REF_DBL)disk_toc) / ((REF_DBL)CLOCKS_PER_SEC),
           ref_mpi_rank(ref_mpi));
  }

  if (chunk == ref_mpi_reduce_chunk_limit(
                   ref_mpi, (ldim + 1) * (REF_INT)sizeof(REF_DBL))) {
    if (ref_mpi_once(ref_mpi)) {
      printf("mpi reduce limited to %d chunks of %d bytes\n", nchunk,
             chunk * (ldim + 1) * (REF_INT)sizeof(REF_DBL));
    }
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_scalar_txt(
    REF_NODE ref_node, REF_INT ldim, REF_DBL *scalar, const char *separator,
    REF_BOOL prepend_xyz, FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT chunk;
  REF_DBL *local_xyzm, *xyzm;
  REF_GLOB global, nnode_written, first;
  REF_INT local, n, i, im, nxyz;
  REF_STATUS status;

  nxyz = 0;
  if (prepend_xyz) nxyz = 3;

  chunk = (REF_INT)(ref_node_n_global(ref_node) / ref_mpi_n(ref_mpi) + 1);
  chunk =
      MIN(chunk, ref_mpi_reduce_chunk_limit(
                     ref_mpi, (nxyz + ldim + 1) * (REF_INT)sizeof(REF_DBL)));

  ref_malloc(local_xyzm, (nxyz + ldim + 1) * chunk, REF_DBL);
  ref_malloc(xyzm, (nxyz + ldim + 1) * chunk, REF_DBL);

  nnode_written = 0;
  while (nnode_written < ref_node_n_global(ref_node)) {
    first = nnode_written;
    n = (REF_INT)MIN((REF_GLOB)chunk,
                     ref_node_n_global(ref_node) - nnode_written);

    nnode_written += n;

    for (i = 0; i < (nxyz + ldim + 1) * chunk; i++) local_xyzm[i] = 0.0;

    for (i = 0; i < n; i++) {
      global = first + i;
      status = ref_node_local(ref_node, global, &local);
      RXS(status, REF_NOT_FOUND, "node local failed");
      if (REF_SUCCESS == status &&
          ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, local)) {
        for (im = 0; im < nxyz; im++)
          local_xyzm[im + (nxyz + ldim + 1) * i] =
              ref_node_xyz(ref_node, im, local);
        for (im = 0; im < ldim; im++)
          local_xyzm[nxyz + im + (nxyz + ldim + 1) * i] =
              scalar[im + ldim * local];
        local_xyzm[nxyz + ldim + (nxyz + ldim + 1) * i] = 1.0;
      } else {
        for (im = 0; im < (nxyz + ldim + 1); im++)
          local_xyzm[im + (nxyz + ldim + 1) * i] = 0.0;
      }
    }

    RSS(ref_mpi_sum(ref_mpi, local_xyzm, xyzm, (nxyz + ldim + 1) * n,
                    REF_DBL_TYPE),
        "sum");

    if (ref_mpi_once(ref_mpi))
      for (i = 0; i < n; i++) {
        if (ABS(xyzm[nxyz + ldim + (nxyz + ldim + 1) * i] - 1.0) > 0.1) {
          printf("error gather node " REF_GLOB_FMT " %f\n", first + i,
                 xyzm[nxyz + ldim + (nxyz + ldim + 1) * i]);
        }
        for (im = 0; im < nxyz + ldim - 1; im++) {
          fprintf(file, "%.15e%s", xyzm[im + (nxyz + ldim + 1) * i], separator);
        }
        if (ldim > 0)
          fprintf(file, "%.15e\n",
                  xyzm[(nxyz + ldim - 1) + (nxyz + ldim + 1) * i]);
      }
  }

  ref_free(xyzm);
  ref_free(local_xyzm);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_scalar_solb(REF_GRID ref_grid,
                                                      REF_INT ldim,
                                                      REF_DBL *scalar,
                                                      FILE *file) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT i;
  REF_FILEPOS next_position = 0;
  REF_INT keyword_code, header_size;
  REF_INT code, version, dim;
  REF_INT int_size, fp_size;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  dim = 3;
  if (ref_grid_twod(ref_grid)) dim = 2;

  version = 2;
  if (1 < ref_grid_meshb_version(ref_grid)) {
    version = ref_grid_meshb_version(ref_grid);
  } else {
    if (REF_EXPORT_MESHB_VERTEX_3 < ref_node_n_global(ref_node)) version = 3;
    if (REF_EXPORT_MESHB_VERTEX_4 < ref_node_n_global(ref_node)) version = 4;
  }

  int_size = 4;
  fp_size = 4;
  if (2 < version) fp_size = 8;
  if (3 < version) int_size = 8;
  header_size = 4 + fp_size + int_size;

  if (ref_mpi_once(ref_mpi)) {
    code = 1;
    REIS(1, fwrite(&code, sizeof(int), 1, file), "code");
    REIS(1, fwrite(&version, sizeof(int), 1, file), "version");
    next_position = (REF_FILEPOS)(4 + fp_size + 4) + ftell(file);
    keyword_code = 3;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "dim code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    REIS(1, fwrite(&dim, sizeof(int), 1, file), "dim");
    REIS(next_position, ftell(file), "dim inconsistent");
  }

  if (ref_mpi_once(ref_mpi)) {
    next_position =
        (REF_FILEPOS)header_size + (REF_FILEPOS)(4 + (ldim * 4)) +
        (REF_FILEPOS)ref_node_n_global(ref_node) * (REF_FILEPOS)(ldim * 8) +
        ftell(file);
    keyword_code = 62;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "vertex version code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    RSS(ref_gather_meshb_glob(file, version, ref_node_n_global(ref_node)),
        "nnode");
    keyword_code = ldim; /* one solution at node */
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "n solutions");
    keyword_code = 1; /* solution type 1, scalar */
    for (i = 0; i < ldim; i++) {
      REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "scalar");
    }
  }

  RSS(ref_gather_node_scalar_bin(ref_node, ldim, scalar, file),
      "bin dump in solb");

  if (ref_mpi_once(ref_mpi))
    REIS(next_position, ftell(file), "solb metric record len inconsistent");

  if (ref_mpi_once(ref_mpi)) { /* End */
    keyword_code = 54;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "end kw");
    next_position = 0;
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_node_scalar_sol(REF_GRID ref_grid,
                                                     REF_INT ldim,
                                                     REF_DBL *scalar,
                                                     FILE *file) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT i;
  REF_INT version, dim;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  dim = 3;
  if (ref_grid_twod(ref_grid)) dim = 2;

  version = 2;

  if (ref_mpi_once(ref_mpi)) {
    fprintf(file, "MeshVersionFormatted %d\n\n", version);
    fprintf(file, "Dimension %d\n\n", dim);
  }

  if (ref_mpi_once(ref_mpi)) {
    fprintf(file, "SolAtVertices\n");
    fprintf(file, REF_GLOB_FMT "\n", ref_node_n_global(ref_node));
    fprintf(file, "%d", ldim);
    for (i = 0; i < ldim; i++) {
      fprintf(file, " %d", 1);
    }
    fprintf(file, "\n");
  }

  RSS(ref_gather_node_scalar_txt(ref_node, ldim, scalar, " ", REF_FALSE, file),
      "txt dump in solb");

  if (ref_mpi_once(ref_mpi)) { /* End */
    fprintf(file, "\nEnd\n");
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_cell(
    REF_NODE ref_node, REF_CELL ref_cell, REF_BOOL faceid_insted_of_c2n,
    REF_BOOL always_id, REF_BOOL swap_endian, REF_BOOL sixty_four_bit,
    REF_BOOL select_faceid, REF_INT faceid, REF_BOOL pad, FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT cell, node, part;
  REF_INT nodes[REF_CELL_MAX_SIZE_PER];
  REF_LONG globals[REF_CELL_MAX_SIZE_PER + 1];
  REF_INT node_per = ref_cell_node_per(ref_cell);
  REF_INT size_per = ref_cell_size_per(ref_cell);
  REF_INT ncell;
  REF_GLOB *c2n;
  REF_INT c2n_int;
  REF_LONG c2n_long;
  REF_INT proc;

  if (ref_mpi_once(ref_mpi)) {
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part &&
          (!select_faceid || nodes[ref_cell_node_per(ref_cell)] == faceid)) {
        for (node = 0; node < node_per; node++)
          globals[node] = ref_node_global(ref_node, nodes[node]) + 1;
        globals[node_per] = REF_EXPORT_MESHB_3D_ID;
        if (size_per > node_per) globals[node_per] = nodes[node_per];

        if (always_id && REF_CELL_PYR == ref_cell_type(ref_cell)) {
          REF_LONG n0, n1, n2, n3, n4;
          /* convention: square basis is 0-1-2-3
             (oriented counter clockwise like trias) and top vertex is 4 */
          n0 = globals[0];
          n1 = globals[3];
          n2 = globals[4];
          n3 = globals[1];
          n4 = globals[2];
          globals[0] = n0;
          globals[1] = n1;
          globals[2] = n2;
          globals[3] = n3;
          globals[4] = n4;
        }

        if (faceid_insted_of_c2n) {
          if (sixty_four_bit) {
            c2n_long = globals[node_per];
            if (swap_endian) SWAP_LONG(c2n_long);
            REIS(1, fwrite(&(c2n_long), sizeof(REF_LONG), 1, file), "long id");
          } else {
            c2n_int = (REF_INT)globals[node_per];
            if (swap_endian) SWAP_INT(c2n_int);
            REIS(1, fwrite(&(c2n_int), sizeof(REF_INT), 1, file), "int id");
          }
        } else {
          for (node = 0; node < node_per; node++) {
            if (sixty_four_bit) {
              c2n_long = globals[node];
              if (swap_endian) SWAP_LONG(c2n_long);
              REIS(1, fwrite(&c2n_long, sizeof(REF_LONG), 1, file),
                   "long cel node");
            } else {
              c2n_int = (REF_INT)globals[node];
              if (swap_endian) SWAP_INT(c2n_int);
              REIS(1, fwrite(&c2n_int, sizeof(REF_INT), 1, file),
                   "int cel node");
            }
          }
          if (pad) {
            REF_INT zero = 0;
            if (swap_endian) SWAP_INT(zero);
            REIS(1, fwrite(&(zero), sizeof(REF_INT), 1, file), "zero pad");
          }
          if (always_id) {
            if (sixty_four_bit) {
              c2n_long = globals[node_per];
              if (swap_endian) SWAP_LONG(c2n_long);
              REIS(1, fwrite(&(c2n_long), sizeof(REF_LONG), 1, file),
                   "long id");
            } else {
              c2n_int = (REF_INT)globals[node_per];
              if (swap_endian) SWAP_INT(c2n_int);
              REIS(1, fwrite(&(c2n_int), sizeof(REF_INT), 1, file), "int id");
            }
          }
        }
      }
    }
  }

  if (ref_mpi_once(ref_mpi)) {
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ncell, 1, REF_INT_TYPE, proc),
          "recv ncell");
      if (ncell > 0) {
        ref_malloc(c2n, ncell * size_per, REF_GLOB);
        RSS(ref_mpi_gather_recv(ref_mpi, c2n, ncell * size_per, REF_GLOB_TYPE,
                                proc),
            "recv c2n");
        for (cell = 0; cell < ncell; cell++) {
          for (node = 0; node < node_per; node++)
            globals[node] = c2n[node + size_per * cell] + 1;
          globals[node_per] = REF_EXPORT_MESHB_3D_ID;
          if (size_per > node_per)
            globals[node_per] = c2n[node_per + size_per * cell];

          if (always_id && REF_CELL_PYR == ref_cell_type(ref_cell)) {
            REF_LONG n0, n1, n2, n3, n4;
            /* convention: square basis is 0-1-2-3
               (oriented counter clockwise like trias) and top vertex is 4 */
            n0 = globals[0];
            n1 = globals[3];
            n2 = globals[4];
            n3 = globals[1];
            n4 = globals[2];
            globals[0] = n0;
            globals[1] = n1;
            globals[2] = n2;
            globals[3] = n3;
            globals[4] = n4;
          }

          if (faceid_insted_of_c2n) {
            if (sixty_four_bit) {
              c2n_long = globals[node_per];
              if (swap_endian) SWAP_LONG(c2n_long);
              REIS(1, fwrite(&(c2n_long), sizeof(REF_LONG), 1, file),
                   "long id");
            } else {
              c2n_int = (REF_INT)globals[node_per];
              if (swap_endian) SWAP_INT(c2n_int);
              REIS(1, fwrite(&(c2n_int), sizeof(REF_INT), 1, file), "int id");
            }
          } else {
            for (node = 0; node < node_per; node++) {
              if (sixty_four_bit) {
                c2n_long = globals[node];
                if (swap_endian) SWAP_LONG(c2n_long);
                REIS(1, fwrite(&c2n_long, sizeof(REF_LONG), 1, file),
                     "long cel node");
              } else {
                c2n_int = (REF_INT)globals[node];
                if (swap_endian) SWAP_INT(c2n_int);
                REIS(1, fwrite(&c2n_int, sizeof(REF_INT), 1, file),
                     "int cel node");
              }
            }
            if (pad) {
              REF_INT zero = 0;
              if (swap_endian) SWAP_INT(zero);
              REIS(1, fwrite(&(zero), sizeof(REF_INT), 1, file), "zero pad");
            }
            if (always_id) {
              if (sixty_four_bit) {
                c2n_long = globals[node_per];
                if (swap_endian) SWAP_LONG(c2n_long);
                REIS(1, fwrite(&(c2n_long), sizeof(REF_LONG), 1, file),
                     "long id");
              } else {
                c2n_int = (REF_INT)globals[node_per];
                if (swap_endian) SWAP_INT(c2n_int);
                REIS(1, fwrite(&(c2n_int), sizeof(REF_INT), 1, file), "int id");
              }
            }
          }
        }
        ref_free(c2n);
      }
    }
  } else {
    ncell = 0;
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part &&
          (!select_faceid || nodes[ref_cell_node_per(ref_cell)] == faceid))
        ncell++;
    }
    RSS(ref_mpi_gather_send(ref_mpi, &ncell, 1, REF_INT_TYPE), "send ncell");
    if (ncell > 0) {
      ref_malloc(c2n, ncell * size_per, REF_GLOB);
      ncell = 0;
      each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
        RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
        if (ref_mpi_rank(ref_mpi) == part &&
            (!select_faceid || nodes[ref_cell_node_per(ref_cell)] == faceid)) {
          for (node = 0; node < node_per; node++)
            c2n[node + size_per * ncell] =
                ref_node_global(ref_node, nodes[node]);
          for (node = node_per; node < size_per; node++)
            c2n[node + size_per * ncell] = (REF_GLOB)nodes[node];
          ncell++;
        }
      }
      RSS(ref_mpi_gather_send(ref_mpi, c2n, ncell * size_per, REF_GLOB_TYPE),
          "send c2n");
      ref_free(c2n);
    }
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_geom(REF_NODE ref_node, REF_GEOM ref_geom,
                                          REF_INT version, REF_INT type,
                                          FILE *file) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT geom, id, i;
  REF_GLOB node;
  REF_INT ngeom;
  REF_GLOB *node_id;
  REF_DBL *param;
  REF_INT proc;
  double double_gref;

  if (ref_mpi_once(ref_mpi)) {
    each_ref_geom_of(ref_geom, type, geom) {
      if (ref_mpi_rank(ref_mpi) !=
          ref_node_part(ref_node, ref_geom_node(ref_geom, geom)))
        continue;
      node = ref_node_global(ref_node, ref_geom_node(ref_geom, geom)) + 1;
      id = ref_geom_id(ref_geom, geom);
      double_gref = (double)ref_geom_gref(ref_geom, geom);
      RSS(ref_gather_meshb_glob(file, version, node), "node");
      RSS(ref_gather_meshb_int(file, version, id), "id");
      for (i = 0; i < type; i++)
        REIS(1,
             fwrite(&(ref_geom_param(ref_geom, i, geom)), sizeof(double), 1,
                    file),
             "id");
      if (0 < type)
        REIS(1, fwrite(&(double_gref), sizeof(double), 1, file), "id");
    }
  }

  if (ref_mpi_once(ref_mpi)) {
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ngeom, 1, REF_INT_TYPE, proc),
          "recv ngeom");
      if (ngeom > 0) {
        ref_malloc(node_id, 3 * ngeom, REF_GLOB);
        ref_malloc(param, 2 * ngeom, REF_DBL);
        RSS(ref_mpi_gather_recv(ref_mpi, node_id, 3 * ngeom, REF_GLOB_TYPE,
                                proc),
            "recv node_id");
        RSS(ref_mpi_gather_recv(ref_mpi, param, 2 * ngeom, REF_DBL_TYPE, proc),
            "recv param");
        for (geom = 0; geom < ngeom; geom++) {
          node = node_id[0 + 3 * geom] + 1;
          id = (REF_INT)node_id[1 + 3 * geom];
          double_gref = (double)node_id[2 + 3 * geom];
          RSS(ref_gather_meshb_glob(file, version, node), "node");
          RSS(ref_gather_meshb_int(file, version, id), "id");
          for (i = 0; i < type; i++)
            REIS(1, fwrite(&(param[i + 2 * geom]), sizeof(double), 1, file),
                 "id");
          if (0 < type)
            REIS(1, fwrite(&(double_gref), sizeof(double), 1, file), "id");
        }
        ref_free(param);
        ref_free(node_id);
      }
    }
  } else {
    ngeom = 0;
    each_ref_geom_of(ref_geom, type, geom) {
      if (ref_mpi_rank(ref_mpi) !=
          ref_node_part(ref_node, ref_geom_node(ref_geom, geom)))
        continue;
      ngeom++;
    }
    RSS(ref_mpi_gather_send(ref_mpi, &ngeom, 1, REF_INT_TYPE), "send ngeom");
    if (ngeom > 0) {
      ref_malloc(node_id, 3 * ngeom, REF_GLOB);
      ref_malloc_init(param, 2 * ngeom, REF_DBL, 0.0); /* prevent uninit */
      ngeom = 0;
      each_ref_geom_of(ref_geom, type, geom) {
        if (ref_mpi_rank(ref_mpi) !=
            ref_node_part(ref_node, ref_geom_node(ref_geom, geom)))
          continue;
        node_id[0 + 3 * ngeom] =
            ref_node_global(ref_node, ref_geom_node(ref_geom, geom));
        node_id[1 + 3 * ngeom] = (REF_GLOB)ref_geom_id(ref_geom, geom);
        node_id[2 + 3 * ngeom] = (REF_GLOB)ref_geom_gref(ref_geom, geom);
        for (i = 0; i < type; i++)
          param[i + 2 * ngeom] = ref_geom_param(ref_geom, i, geom);
        ngeom++;
      }
      RSS(ref_mpi_gather_send(ref_mpi, node_id, 3 * ngeom, REF_GLOB_TYPE),
          "send node_id");
      RSS(ref_mpi_gather_send(ref_mpi, param, 2 * ngeom, REF_DBL_TYPE),
          "send param");
      ref_free(param);
      ref_free(node_id);
    }
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_meshb(REF_GRID ref_grid,
                                           const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_INT code, version, dim;
  REF_FILEPOS next_position = 0;
  REF_INT keyword_code, header_size, int_size, fp_size;
  REF_LONG ncell;
  REF_INT node_per;
  REF_INT ngeom, type, group;
  REF_CELL ref_cell;
  REF_GEOM ref_geom = ref_grid_geom(ref_grid);
  REF_BOOL faceid_insted_of_c2n = REF_FALSE;
  REF_BOOL always_id = REF_TRUE;
  REF_BOOL swap_endian = REF_FALSE;
  REF_BOOL sixty_four_bit = REF_FALSE;
  REF_BOOL select_faceid = REF_FALSE;
  REF_INT faceid = REF_EMPTY;
  REF_BOOL pad = REF_FALSE;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  dim = 3;
  if (ref_grid_twod(ref_grid)) dim = 2;

  version = 2;
  if (1 < ref_grid_meshb_version(ref_grid)) {
    version = ref_grid_meshb_version(ref_grid);
  } else {
    if (REF_EXPORT_MESHB_VERTEX_3 < ref_node_n_global(ref_node)) version = 3;
    if (REF_EXPORT_MESHB_VERTEX_4 < ref_node_n_global(ref_node)) version = 4;
  }

  if (4 <= version) sixty_four_bit = REF_TRUE;

  int_size = 4;
  fp_size = 4;
  if (2 < version) fp_size = 8;
  if (3 < version) int_size = 8;
  header_size = 4 + fp_size + int_size;

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    code = 1;
    REIS(1, fwrite(&code, sizeof(int), 1, file), "code");
    REIS(1, fwrite(&version, sizeof(int), 1, file), "version");
    /* dimension keyword always int */
    next_position = (REF_FILEPOS)(4 + fp_size + 4) + ftell(file);
    keyword_code = 3;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "dim code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    REIS(1, fwrite(&dim, sizeof(int), 1, file), "dim");
    REIS(next_position, ftell(file), "dim inconsistent");
  }

  if (ref_grid_once(ref_grid)) {
    next_position = (REF_FILEPOS)header_size +
                    (REF_FILEPOS)ref_node_n_global(ref_node) *
                        (REF_FILEPOS)(dim * 8 + int_size) +
                    ftell(file);
    keyword_code = 4;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "vertex version code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    RSS(ref_gather_meshb_glob(file, version, ref_node_n_global(ref_node)),
        "nnode");
  }
  RSS(ref_gather_node(ref_node, swap_endian, version, ref_grid_twod(ref_grid),
                      file),
      "nodes");
  if (ref_grid_once(ref_grid))
    REIS(next_position, ftell(file), "vertex inconsistent");

  each_ref_grid_all_ref_cell(ref_grid, group, ref_cell) {
    RSS(ref_cell_ncell(ref_cell, ref_node, &ncell), "ncell");
    if (ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        RSS(ref_cell_meshb_keyword(ref_cell, &keyword_code), "kw");
        node_per = ref_cell_node_per(ref_cell);
        next_position =
            ftell(file) + (REF_FILEPOS)header_size +
            (REF_FILEPOS)ncell * (REF_FILEPOS)(int_size * (node_per + 1));
        REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "keyword code");
        RSS(ref_export_meshb_next_position(file, version, next_position),
            "next");
        RSS(ref_gather_meshb_glob(file, version, ncell), "ncell");
      }
      RSS(ref_gather_cell(ref_node, ref_cell, faceid_insted_of_c2n, always_id,
                          swap_endian, sixty_four_bit, select_faceid, faceid,
                          pad, file),
          "nodes");
      if (ref_grid_once(ref_grid))
        REIS(next_position, ftell(file), "cell inconsistent");
    }
  }

  each_ref_type(ref_geom, type) {
    keyword_code = 40 + type; /* GmfVerticesOnGeometricVertices */
    RSS(ref_gather_ngeom(ref_node, ref_geom, type, &ngeom), "ngeom");
    if (ngeom > 0) {
      if (ref_grid_once(ref_grid)) {
        next_position =
            (REF_FILEPOS)header_size +
            (REF_FILEPOS)ngeom * (REF_FILEPOS)(int_size * 2 + 8 * type) +
            (0 < type ? 8 * ngeom : 0) + ftell(file);
        REIS(1, fwrite(&keyword_code, sizeof(int), 1, file),
             "vertex version code");
        RSS(ref_export_meshb_next_position(file, version, next_position), "np");
        RSS(ref_gather_meshb_int(file, version, ngeom), "ngeom");
      }
      RSS(ref_gather_geom(ref_node, ref_geom, version, type, file), "nodes");
      if (ref_grid_once(ref_grid))
        REIS(next_position, ftell(file), "geom inconsistent");
    }
  }

  if (ref_grid_once(ref_grid) && 0 < ref_geom_cad_data_size(ref_geom)) {
    keyword_code = 126; /* GmfByteFlow */
    next_position = (REF_FILEPOS)header_size +
                    (REF_FILEPOS)ref_geom_cad_data_size(ref_geom) + ftell(file);
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "keyword");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    RSS(ref_gather_meshb_size(file, version, ref_geom_cad_data_size(ref_geom)),
        "cad size");
    REIS(ref_geom_cad_data_size(ref_geom),
         fwrite(ref_geom_cad_data(ref_geom), sizeof(REF_BYTE),
                (size_t)ref_geom_cad_data_size(ref_geom), file),
         "node");
    REIS(next_position, ftell(file), "cad_model inconsistent");
  }

  if (ref_grid_once(ref_grid)) { /* End */
    keyword_code = 54;           /* GmfEnd 101-47 */
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "vertex version code");
    next_position = 0;
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    fclose(file);
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_avm(REF_GRID ref_grid,
                                         const char *filename) {
  FILE *file;
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_GLOB nnode;
  REF_LONG nedg, ntri, ntet;
  REF_INT nfaceid, min_faceid, max_faceid;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  nnode = ref_node_n_global(ref_node);
  RSS(ref_cell_ncell(ref_grid_edg(ref_grid), ref_node, &nedg), "nedg");
  RSS(ref_cell_ncell(ref_grid_tri(ref_grid), ref_node, &ntri), "ntri");
  RSS(ref_cell_ncell(ref_grid_tet(ref_grid), ref_node, &ntet), "ntet");
  if (ref_grid_twod(ref_grid)) {
    RSS(ref_cell_id_range(ref_grid_edg(ref_grid), ref_mpi, &min_faceid,
                          &max_faceid),
        "range");
  } else {
    RSS(ref_grid_faceid_range(ref_grid, &min_faceid, &max_faceid), "range");
  }
  nfaceid = max_faceid - min_faceid + 1;

  file = NULL;
  if (ref_mpi_once(ref_mpi)) {
    char magic_string[] = "AVMESH";
    int magic_number = 1;
    int revision_number = 2;
    int n_meshes = 1;
    char nul = '\0';
    int length, i;
    char contact_info[] = "NASA/refine";
    int precision = 2;
    int dimension;
    char file_description[] = "refine";
    char mesh_name[] = "Sketch2Solution";
    char mesh_type[] = "unstruc";
    char mesh_generator[] = "refine";
    char coordinate_system[7];
    char ref_point_desc[] = "";
    char mesh_description[] = "refineSketch2Solution";
    double model_scale = 1.0;
    char mesh_units[12];
    int refined = 0;
    int n_int;
    char element_scheme[] = "uniform";
    int faceid;
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    REIS(6, fwrite(magic_string, sizeof(char), 6, file), "magic_string");
    REIS(1, fwrite(&magic_number, sizeof(magic_number), 1, file),
         "magic_number");
    REIS(1, fwrite(&revision_number, sizeof(revision_number), 1, file),
         "revision_number");
    REIS(1, fwrite(&n_meshes, sizeof(n_meshes), 1, file), "n_meshes");
    length = (int)strlen(contact_info);
    REIS(length,
         fwrite(contact_info, sizeof(char), (unsigned long)length, file),
         "contact_info");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    REIS(1, fwrite(&precision, sizeof(precision), 1, file), "precision");
    dimension = (ref_grid_twod(ref_grid) ? 2 : 3);
    REIS(1, fwrite(&dimension, sizeof(dimension), 1, file), "dimension");
    length = (int)strlen(file_description);
    REIS(1, fwrite(&length, sizeof(length), 1, file), "length");
    REIS(length,
         fwrite(file_description, sizeof(char), (unsigned long)length, file),
         "file_description");
    length = (int)strlen(mesh_name);
    REIS(length, fwrite(mesh_name, sizeof(char), (unsigned long)length, file),
         "mesh_name");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    length = (int)strlen(mesh_type);
    REIS(length, fwrite(mesh_type, sizeof(char), (unsigned long)length, file),
         "mesh_type");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    length = (int)strlen(mesh_generator);
    REIS(length,
         fwrite(mesh_generator, sizeof(char), (unsigned long)length, file),
         "mesh_generator");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    if (ref_grid_twod(ref_grid)) {
      snprintf(coordinate_system, 7, "xByUzL"); /* 2D: always xByUzL */
    } else {
      if (ref_geom_model_loaded(ref_grid_geom(ref_grid))) {
        const char *coord_system;
        REF_STATUS ref_status;
        ref_status = ref_egads_get_attribute(
            ref_grid_geom(ref_grid), REF_GEOM_BODY, REF_EMPTY,
            "av:coordinate_system", &coord_system);
        if (REF_SUCCESS == ref_status)
          RSS(ref_grid_parse_coordinate_system(ref_grid, coord_system),
              "parse av coor sys");
      }
      switch (ref_grid_coordinate_system(ref_grid)) {
        case REF_GRID_XBYRZU:
          snprintf(coordinate_system, 7, "xByRzU");
          break;
        case REF_GRID_XBYUZL:
          snprintf(coordinate_system, 7, "xByUzL");
          break;
        case REF_GRID_XFYRZD:
          snprintf(coordinate_system, 7, "xFyRzD");
          break;
        case REF_GRID_COORDSYS_LAST:
          THROW("REF_GRID_COORDSYS_LAST");
      }
    }
    length = (int)strlen(coordinate_system);
    REIS(length,
         fwrite(coordinate_system, sizeof(char), (unsigned long)length, file),
         "coordinate_system");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    REIS(1, fwrite(&model_scale, sizeof(model_scale), 1, file), "model_scale");
    if (ref_geom_model_loaded(ref_grid_geom(ref_grid))) {
      const char *unit;
      REF_STATUS ref_status;
      ref_status =
          ref_egads_get_attribute(ref_grid_geom(ref_grid), REF_GEOM_BODY,
                                  REF_EMPTY, "av:mesh_units", &unit);
      if (REF_SUCCESS == ref_status)
        RSS(ref_grid_parse_unit(ref_grid, unit), "parse unit");
    }
    switch (ref_grid_unit(ref_grid)) {
      case REF_GRID_IN:
        snprintf(mesh_units, 12, "in");
        break;
      case REF_GRID_FT:
        snprintf(mesh_units, 12, "ft");
        break;
      case REF_GRID_M:
        snprintf(mesh_units, 12, "m");
        break;
      case REF_GRID_CM:
        snprintf(mesh_units, 12, "cm");
        break;
      case REF_GRID_UNIT_LAST:
        THROW("REF_GRID_UNIT_LAST");
    }
    length = (int)strlen(mesh_units);
    REIS(length, fwrite(mesh_units, sizeof(char), (unsigned long)length, file),
         "mesh_units");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    if (ref_geom_model_loaded(ref_grid_geom(ref_grid))) {
      const REF_DBL *reference;
      REF_STATUS ref_status;
      ref_status = ref_egads_get_real_attribute(
          ref_grid_geom(ref_grid), REF_GEOM_BODY, REF_EMPTY, "av:reference",
          &reference, &length);
      if (REF_SUCCESS == ref_status && 7 == length) {
        for (i = 0; i < length; i++) {
          ref_grid_reference(ref_grid, i) = reference[i];
        }
      }
    }
    REIS(7, fwrite(&ref_grid_reference(ref_grid, 0), sizeof(double), 7, file),
         "reference");
    length = (int)strlen(ref_point_desc);
    REIS(length,
         fwrite(ref_point_desc, sizeof(char), (unsigned long)length, file),
         "ref_point_desc");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    REIS(1, fwrite(&refined, sizeof(refined), 1, file), "refined");
    length = (int)strlen(mesh_description);
    REIS(length,
         fwrite(mesh_description, sizeof(char), (unsigned long)length, file),
         "mesh_description");
    length = 128 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    n_int = (int)nnode;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "nodes");
    if (ref_grid_twod(ref_grid)) {
      n_int = ((int)nedg + 3 * (int)ntri) / 2;
    } else {
      n_int = ((int)ntri + 4 * (int)ntet) / 2;
    }
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "nfaces");
    if (ref_grid_twod(ref_grid)) {
      n_int = (int)ntri;
    } else {
      n_int = (int)ntet;
    }
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "ncells");
    if (ref_grid_twod(ref_grid)) {
      n_int = 2;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "max nodes per face");
      n_int = 3;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "max nodes per cell");
      n_int = 3;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "max faces per cell");
    } else {
      n_int = 3;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "max nodes per face");
      n_int = 4;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "max nodes per cell");
      n_int = 4;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "max faces per cell");
    }
    length = (int)strlen(element_scheme);
    REIS(length,
         fwrite(element_scheme, sizeof(char), (unsigned long)length, file),
         "element_scheme");
    length = 32 - length;
    for (i = 0; i < length; i++) {
      REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
    }
    n_int = 1;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "face polynomial order");
    n_int = 1;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "cell polynomial order");
    n_int = nfaceid;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "# boundary patches");
    n_int = 0;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "nhex");
    if (ref_grid_twod(ref_grid)) {
      n_int = (int)ntri;
    } else {
      n_int = (int)ntet;
    }
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "ntet");
    n_int = 0;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "npri");
    n_int = 0;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "npyr");
    if (ref_grid_twod(ref_grid)) {
      n_int = (int)nedg;
    } else {
      n_int = (int)ntri;
    }
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "# boundary tri faces");
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "# tri faces");
    n_int = 0;
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "# boundary quad faces");
    REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "# quad faces");
    length = 5;
    for (i = 0; i < length; i++) {
      n_int = 0;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "zeros");
    }
    for (faceid = min_faceid; faceid <= max_faceid; faceid++) {
      REF_GEOM ref_geom = ref_grid_geom(ref_grid);
      const char *patch_label, *patch_type;
      const char *unknown_patch_type = "unknown";
      char patch_label_index[33];
      REF_STATUS ref_status;
      REF_INT ref_geom_type = REF_GEOM_FACE;
      if (ref_grid_twod(ref_grid)) ref_geom_type = REF_GEOM_EDGE;
      ref_status = ref_egads_get_attribute(ref_geom, ref_geom_type, faceid,
                                           "av:patch_label", &patch_label);
      if (REF_SUCCESS != ref_status) patch_label = "unknown";
      snprintf(patch_label_index, 33, "%s-%d", patch_label, faceid);
      length = (int)strlen(patch_label_index);
      REIS(length,
           fwrite(patch_label_index, sizeof(char), (unsigned long)length, file),
           "patch_label");
      length = 32 - length;
      for (i = 0; i < length; i++) {
        REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
      }
      ref_status = ref_egads_get_attribute(ref_geom, ref_geom_type, faceid,
                                           "av:patch_type", &patch_type);
      if (REF_SUCCESS != ref_status || NULL == patch_type)
        patch_type = unknown_patch_type;
      length = (int)strlen(patch_type);
      REIS(length,
           fwrite(patch_type, sizeof(char), (unsigned long)length, file),
           "patch_label");
      length = 16 - length;
      for (i = 0; i < length; i++) {
        REIS(1, fwrite(&nul, sizeof(nul), 1, file), "nul");
      }
      n_int = -faceid;
      REIS(1, fwrite(&n_int, sizeof(n_int), 1, file), "patch ID");
    }
  }

  {
    REF_BOOL swap_endian = REF_FALSE;
    REF_INT version = 0; /* meshb version, zero is no id */
    /* twod still has 3 coordinates, with z coordinate ignored/set to zero */
    REF_BOOL twod = REF_FALSE;
    RSS(ref_gather_node(ref_node, swap_endian, version, twod, file), "nodes");
  }

  if (ref_grid_twod(ref_grid)) {
    REF_CELL ref_cell = ref_grid_edg(ref_grid);
    REF_BOOL faceid_insted_of_c2n = REF_FALSE;
    REF_BOOL always_id = REF_TRUE;
    REF_BOOL swap_endian = REF_FALSE;
    REF_BOOL sixty_four_bit = REF_FALSE;
    REF_BOOL select_faceid = REF_FALSE;
    REF_INT faceid = 0;
    REF_BOOL pad = REF_TRUE;
    REF_INT cell;
    each_ref_cell_valid_cell(ref_cell, cell) {
      ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell) =
          -ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell);
    }
    RSS(ref_gather_cell(ref_node, ref_cell, faceid_insted_of_c2n, always_id,
                        swap_endian, sixty_four_bit, select_faceid, faceid, pad,
                        file),
        "nodes");
    each_ref_cell_valid_cell(ref_cell, cell) {
      ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell) =
          -ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell);
    }
  } else {
    REF_CELL ref_cell = ref_grid_tri(ref_grid);
    REF_BOOL faceid_insted_of_c2n = REF_FALSE;
    REF_BOOL always_id = REF_TRUE;
    REF_BOOL swap_endian = REF_FALSE;
    REF_BOOL sixty_four_bit = REF_FALSE;
    REF_BOOL select_faceid = REF_FALSE;
    REF_INT faceid = 0;
    REF_BOOL pad = REF_FALSE;
    REF_INT cell;
    each_ref_cell_valid_cell(ref_cell, cell) {
      ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell) =
          -ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell);
    }
    RSS(ref_gather_cell(ref_node, ref_cell, faceid_insted_of_c2n, always_id,
                        swap_endian, sixty_four_bit, select_faceid, faceid, pad,
                        file),
        "nodes");
    each_ref_cell_valid_cell(ref_cell, cell) {
      ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell) =
          -ref_cell_c2n(ref_cell, ref_cell_id_index(ref_cell), cell);
    }
  }

  if (ref_grid_twod(ref_grid)) {
    REF_CELL ref_cell = ref_grid_tri(ref_grid);
    REF_BOOL faceid_insted_of_c2n = REF_FALSE;
    REF_BOOL always_id = REF_FALSE;
    REF_BOOL swap_endian = REF_FALSE;
    REF_BOOL sixty_four_bit = REF_FALSE;
    REF_BOOL select_faceid = REF_FALSE;
    REF_INT faceid = 0;
    REF_BOOL pad = REF_TRUE;
    REF_INT cell, temp_node;
    /* avm winds tri different than EGADS */
    each_ref_cell_valid_cell(ref_cell, cell) {
      temp_node = ref_cell_c2n(ref_cell, 2, cell);
      ref_cell_c2n(ref_cell, 2, cell) = ref_cell_c2n(ref_cell, 1, cell);
      ref_cell_c2n(ref_cell, 1, cell) = temp_node;
    }
    RSS(ref_gather_cell(ref_node, ref_cell, faceid_insted_of_c2n, always_id,
                        swap_endian, sixty_four_bit, select_faceid, faceid, pad,
                        file),
        "nodes");
    /* wind back (flip) after write */
    each_ref_cell_valid_cell(ref_cell, cell) {
      temp_node = ref_cell_c2n(ref_cell, 2, cell);
      ref_cell_c2n(ref_cell, 2, cell) = ref_cell_c2n(ref_cell, 1, cell);
      ref_cell_c2n(ref_cell, 1, cell) = temp_node;
    }
  } else {
    REF_CELL ref_cell = ref_grid_tet(ref_grid);
    REF_BOOL faceid_insted_of_c2n = REF_FALSE;
    REF_BOOL always_id = REF_FALSE;
    REF_BOOL swap_endian = REF_FALSE;
    REF_BOOL sixty_four_bit = REF_FALSE;
    REF_BOOL select_faceid = REF_FALSE;
    REF_INT faceid = 0;
    REF_BOOL pad = REF_FALSE;
    RSS(ref_gather_cell(ref_node, ref_cell, faceid_insted_of_c2n, always_id,
                        swap_endian, sixty_four_bit, select_faceid, faceid, pad,
                        file),
        "nodes");
  }

  if (ref_mpi_once(ref_mpi)) fclose(file);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_bin_ugrid(REF_GRID ref_grid,
                                               const char *filename,
                                               REF_BOOL swap_endian,
                                               REF_BOOL sixty_four_bit) {
  FILE *file;
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_GLOB nnode;
  REF_LONG ntri, nqua, ntet, npyr, npri, nhex;
  REF_LONG size_long;
  REF_INT size_int;
  REF_CELL ref_cell;
  REF_INT group;
  REF_INT faceid;
  REF_BOOL version = 0; /* meshb version, zero is no id */
  REF_BOOL faceid_insted_of_c2n, select_faceid;
  REF_BOOL pad = REF_FALSE;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  nnode = ref_node_n_global(ref_node);

  RSS(ref_cell_ncell(ref_grid_tri(ref_grid), ref_node, &ntri), "ntri");
  RSS(ref_cell_ncell(ref_grid_qua(ref_grid), ref_node, &nqua), "nqua");

  RSS(ref_cell_ncell(ref_grid_tet(ref_grid), ref_node, &ntet), "ntet");
  RSS(ref_cell_ncell(ref_grid_pyr(ref_grid), ref_node, &npyr), "npyr");
  RSS(ref_cell_ncell(ref_grid_pri(ref_grid), ref_node, &npri), "npri");
  RSS(ref_cell_ncell(ref_grid_hex(ref_grid), ref_node, &nhex), "nhex");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    if (sixty_four_bit) {
      size_long = (REF_LONG)nnode;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "nnode");

      size_long = (REF_LONG)ntri;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "ntri");
      size_long = (REF_LONG)nqua;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "nqua");

      size_long = (REF_LONG)ntet;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "ntet");
      size_long = (REF_LONG)npyr;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "npyr");
      size_long = (REF_LONG)npri;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "npri");
      size_long = (REF_LONG)nhex;
      if (swap_endian) SWAP_LONG(size_long);
      REIS(1, fwrite(&size_long, sizeof(REF_LONG), 1, file), "nhex");
    } else {
      size_int = (REF_INT)nnode;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "nnode");

      size_int = (REF_INT)ntri;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "ntri");
      size_int = (REF_INT)nqua;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "nqua");

      size_int = (REF_INT)ntet;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "ntet");
      size_int = (REF_INT)npyr;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "npyr");
      size_int = (REF_INT)npri;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "npri");
      size_int = (REF_INT)nhex;
      if (swap_endian) SWAP_INT(size_int);
      REIS(1, fwrite(&size_int, sizeof(REF_INT), 1, file), "nhex");
    }
  }
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "ugrid header");

  RSS(ref_gather_node(ref_node, swap_endian, version, REF_FALSE, file),
      "nodes");

  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "ugrid node");

  faceid_insted_of_c2n = REF_FALSE;
  select_faceid = REF_FALSE;
  faceid = REF_EMPTY;
  RSS(ref_gather_cell(ref_node, ref_grid_tri(ref_grid), faceid_insted_of_c2n,
                      version, swap_endian, sixty_four_bit, select_faceid,
                      faceid, pad, file),
      "tri c2n");
  RSS(ref_gather_cell(ref_node, ref_grid_qua(ref_grid), faceid_insted_of_c2n,
                      version, swap_endian, sixty_four_bit, select_faceid,
                      faceid, pad, file),
      "qua c2n");

  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "ugrid face write");

  faceid_insted_of_c2n = REF_TRUE;
  select_faceid = REF_FALSE;
  faceid = REF_EMPTY;
  RSS(ref_gather_cell(ref_node, ref_grid_tri(ref_grid), faceid_insted_of_c2n,
                      version, swap_endian, sixty_four_bit, select_faceid,
                      faceid, pad, file),
      "tri faceid");
  RSS(ref_gather_cell(ref_node, ref_grid_qua(ref_grid), faceid_insted_of_c2n,
                      version, swap_endian, sixty_four_bit, select_faceid,
                      faceid, pad, file),
      "qua faceid");
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "ugrid faceid write");

  faceid_insted_of_c2n = REF_FALSE;
  select_faceid = REF_FALSE;
  faceid = REF_EMPTY;
  each_ref_grid_3d_ref_cell(ref_grid, group, ref_cell) {
    RSS(ref_gather_cell(ref_node, ref_cell, faceid_insted_of_c2n, version,
                        swap_endian, sixty_four_bit, select_faceid, faceid, pad,
                        file),
        "cell c2n");
    if (0 < ref_mpi_timing(ref_mpi))
      ref_mpi_stopwatch_stop(ref_mpi, "ugrid vol cell write");
  }

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_by_extension(REF_GRID ref_grid,
                                           const char *filename) {
  size_t end_of_string;

  end_of_string = strlen(filename);

  if (end_of_string > 4 && (strcmp(&filename[end_of_string - 4], ".tec") == 0 ||
                            strcmp(&filename[end_of_string - 4], ".dat") == 0 ||
                            strcmp(&filename[end_of_string - 2], ".t") == 0)) {
    RSS(ref_gather_tec(ref_grid, filename), "scalar tec");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 &&
      (strcmp(&filename[end_of_string - 4], ".avm") == 0)) {
    RSS(ref_gather_avm(ref_grid, filename), "scalar plt");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 &&
      (strcmp(&filename[end_of_string - 4], ".plt") == 0)) {
    RSS(ref_gather_scalar_by_extension(ref_grid, 0, NULL, NULL, filename),
        "scalar plt");
    return REF_SUCCESS;
  }
  if (end_of_string > 10 &&
      strcmp(&filename[end_of_string - 10], ".lb8.ugrid") == 0) {
    RSS(ref_gather_bin_ugrid(ref_grid, filename, REF_FALSE, REF_FALSE),
        ".lb8.ugrid failed");
    return REF_SUCCESS;
  }
  if (end_of_string > 9 &&
      strcmp(&filename[end_of_string - 9], ".b8.ugrid") == 0) {
    RSS(ref_gather_bin_ugrid(ref_grid, filename, REF_TRUE, REF_FALSE),
        ".b8.ugrid failed");
    return REF_SUCCESS;
  }
  if (end_of_string > 11 &&
      strcmp(&filename[end_of_string - 11], ".lb8l.ugrid") == 0) {
    RSS(ref_gather_bin_ugrid(ref_grid, filename, REF_FALSE, REF_TRUE),
        ".lb8l.ugrid failed");
    return REF_SUCCESS;
  }
  if (end_of_string > 10 &&
      strcmp(&filename[end_of_string - 10], ".b8l.ugrid") == 0) {
    RSS(ref_gather_bin_ugrid(ref_grid, filename, REF_TRUE, REF_TRUE),
        ".b8l.ugrid failed");
    return REF_SUCCESS;
  }
  if (end_of_string > 12 &&
      strcmp(&filename[end_of_string - 12], ".lb8.ugrid64") == 0) {
    RSS(ref_gather_bin_ugrid(ref_grid, filename, REF_FALSE, REF_TRUE),
        ".lb8.ugrid64 failed");
    return REF_SUCCESS;
  }
  if (end_of_string > 11 &&
      strcmp(&filename[end_of_string - 11], ".b8.ugrid64") == 0) {
    RSS(ref_gather_bin_ugrid(ref_grid, filename, REF_TRUE, REF_TRUE),
        ".b8.ugrid64 failed");
    return REF_SUCCESS;
  }
  if (end_of_string > 6 &&
      strcmp(&filename[end_of_string - 6], ".meshb") == 0) {
    RSS(ref_gather_meshb(ref_grid, filename), "meshb failed");
    return REF_SUCCESS;
  }
  printf("%s: %d: %s %s\n", __FILE__, __LINE__,
         "output file name extension unknown", filename);
  return REF_FAILURE;
}

REF_FCN REF_STATUS ref_gather_metric(REF_GRID ref_grid, const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);
  size_t end_of_string;
  REF_BOOL solb_format = REF_FALSE;
  REF_BOOL met_format = REF_FALSE;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    end_of_string = strlen(filename);
    if (end_of_string > 5 && strcmp(&filename[end_of_string - 5], ".solb") == 0)
      solb_format = REF_TRUE;
    if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".met") == 0)
      met_format = REF_TRUE;
  }
  RSS(ref_mpi_all_or(ref_grid_mpi(ref_grid), &solb_format), "bcast");
  RSS(ref_mpi_all_or(ref_grid_mpi(ref_grid), &met_format), "bcast");

  if (solb_format) {
    RSS(ref_gather_node_metric_solb(ref_grid, file), "nodes");
  } else if (met_format) {
    RSS(ref_gather_node_bamg_met(ref_grid, file), "nodes");
  } else {
    RSS(ref_gather_node_metric(ref_node, file), "nodes");
  }

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_txt(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char *separator,
                                                const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
  }

  RSS(ref_gather_node_scalar_txt(ref_node, ldim, scalar, separator, REF_FALSE,
                                 file),
      "nodes");

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_bin(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
  }

  RSS(ref_gather_node_scalar_bin(ref_node, ldim, scalar, file), "nodes");

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_solb(REF_GRID ref_grid,
                                                 REF_INT ldim, REF_DBL *scalar,
                                                 const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
  }

  RSS(ref_gather_node_scalar_solb(ref_grid, ldim, scalar, file), "nodes");

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_cell_restart_sol(
    REF_GRID ref_grid, REF_INT ldim, REF_DBL *scalar, const char *filename) {
  FILE *file;
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_CELL ref_cell = ref_grid_tri(ref_grid);
  REF_INT cell, nodes[REF_CELL_MAX_SIZE_PER], cell_node;

  RAS(!ref_mpi_para(ref_mpi), "only implemented for single core");
  REIS(5, ldim, "only implemented for ldim=5");

  file = fopen(filename, "w");
  if (NULL == (void *)file) printf("unable to open %s\n", filename);
  RNS(file, "unable to open file");

  fprintf(file, "%d\n", ref_cell_n(ref_cell));

  each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
    REF_DBL rho, u, v, p;
    rho = 0;
    u = 0;
    v = 0;
    p = 0;
    each_ref_cell_cell_node(ref_cell, cell_node) {
      rho += scalar[0 + ldim * nodes[cell_node]];
      u += scalar[1 + ldim * nodes[cell_node]];
      v += scalar[3 + ldim * nodes[cell_node]];
      p += scalar[4 + ldim * nodes[cell_node]];
    }
    rho /= (REF_DBL)ref_cell_node_per(ref_cell);
    u /= (REF_DBL)ref_cell_node_per(ref_cell);
    v /= (REF_DBL)ref_cell_node_per(ref_cell);
    p /= (REF_DBL)ref_cell_node_per(ref_cell);
    fprintf(file, "%.15e %.15e %.15e %.15e\n", rho, u, v, p);
  }

  fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_sol(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
  }

  RSS(ref_gather_node_scalar_sol(ref_grid, ldim, scalar, file), "nodes");

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_scalar_cell_solb(REF_GRID ref_grid, REF_INT ldim,
                                               REF_DBL *scalar,
                                               const char *filename) {
  FILE *file;
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell = NULL;
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_LONG ncell_local, ncell, ntet, npri;
  int version, code, keyword_code, dim, cell_keyword;
  REF_INT i, header_size, ncell_int;
  REF_FILEPOS next_position;
  REF_INT part, cell, nodes[REF_CELL_MAX_SIZE_PER];
  REF_DBL cell_average, *data;
  REF_INT node, j, proc;
  REF_LONG ncell_recv;

  next_position = 0;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  RSS(ref_cell_ncell(ref_grid_tet(ref_grid), ref_node, &ntet), "ntet");
  RSS(ref_cell_ncell(ref_grid_pri(ref_grid), ref_node, &npri), "npri");
  cell_keyword = REF_EMPTY;
  if (ntet > 0 && npri == 0) {
    /* GmfSolAtTetrahedra 113 - 47 = 66 */
    cell_keyword = 66;
    ref_cell = ref_grid_tet(ref_grid);
  }
  if (ntet == 0 && npri > 0) {
    /* GmfSolAtTetrahedra 114 - 47 = 67 */
    cell_keyword = 67;
    ref_cell = ref_grid_pri(ref_grid);
  }
  RUS(REF_EMPTY, cell_keyword, "grid must be all tet or all prism");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
  }

  if (REF_EXPORT_MESHB_VERTEX_3 < ntet + npri) {
    version = 3;
    header_size = 4 + 8 + 4;
  } else {
    version = 2;
    header_size = 4 + 4 + 4;
  }

  if (ref_mpi_once(ref_mpi)) {
    code = 1;
    REIS(1, fwrite(&code, sizeof(int), 1, file), "code");
    REIS(1, fwrite(&version, sizeof(int), 1, file), "version");
    next_position = (REF_FILEPOS)header_size + ftell(file);
    keyword_code = 3;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "dim code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    dim = 3;
    REIS(1, fwrite(&dim, sizeof(int), 1, file), "dim");
    REIS(next_position, ftell(file), "dim inconsistent");
  }

  ncell_local = 0;
  each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
    RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
    if (ref_mpi_rank(ref_mpi) == part) {
      ncell_local++;
    }
  }
  ncell = ncell_local;
  RSS(ref_mpi_allsum(ref_mpi, &ncell, 1, REF_LONG_TYPE), "sum");

  RAS(ncell < (REF_LONG)REF_INT_MAX, "requires version 4 solb for 64bit ncell");

  if (ref_mpi_once(ref_mpi)) {
    next_position = (REF_FILEPOS)header_size + (REF_FILEPOS)(4 + (ldim * 4)) +
                    (REF_FILEPOS)ncell * (REF_FILEPOS)(ldim * 8) + ftell(file);
    /* GmfSolAtTetrahedra 113 - 47 = 66 */
    keyword_code = cell_keyword;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "keyword code");
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
    ncell_int = (int)ncell;
    REIS(1, fwrite(&ncell_int, sizeof(int), 1, file), "nnode");
    keyword_code = ldim; /* one solution at node */
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "n solutions");
    keyword_code = 1; /* solution type 1, scalar */
    for (i = 0; i < ldim; i++) {
      REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "scalar");
    }
  }

  if (ref_mpi_once(ref_mpi)) {
    each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
      RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
      if (ref_mpi_rank(ref_mpi) == part) {
        for (i = 0; i < ldim; i++) {
          cell_average = 0.0;
          for (node = 0; node < ref_cell_node_per(ref_cell); node++)
            cell_average += scalar[i + ldim * nodes[node]];
          cell_average /= (REF_DBL)ref_cell_node_per(ref_cell);
          REIS(1, fwrite(&cell_average, sizeof(REF_DBL), 1, file), "cell avg");
        }
      }
    }
    each_ref_mpi_worker(ref_mpi, proc) {
      RSS(ref_mpi_gather_recv(ref_mpi, &ncell_recv, 1, REF_LONG_TYPE, proc),
          "recv ncell");
      if (ncell_recv > 0) {
        ref_malloc(data, ldim * ncell_recv, REF_DBL);
        RSS(ref_mpi_gather_recv(ref_mpi, data, (REF_INT)(ldim * ncell_recv),
                                REF_DBL_TYPE, proc),
            "send data");
        REIS((size_t)(ldim * ncell_recv),
             fwrite(data, sizeof(REF_DBL), (size_t)(ldim * ncell_recv), file),
             "worker cell avg");
        ref_free(data);
      }
    }
  } else {
    RSS(ref_mpi_gather_send(ref_mpi, &ncell_local, 1, REF_LONG_TYPE),
        "send ncell");
    if (ncell_local > 0) {
      ref_malloc(data, ldim * ncell_local, REF_DBL);
      j = 0;
      each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
        RSS(ref_cell_part(ref_cell, ref_node, cell, &part), "part");
        if (ref_mpi_rank(ref_mpi) == part) {
          for (i = 0; i < ldim; i++) {
            cell_average = 0.0;
            for (node = 0; node < ref_cell_node_per(ref_cell); node++) {
              cell_average += scalar[i + ldim * nodes[node]];
            }
            cell_average /= (REF_DBL)ref_cell_node_per(ref_cell);
            data[i + ldim * j] = cell_average;
          }
          j++;
        }
      }
      RSS(ref_mpi_gather_send(ref_mpi, data, (REF_INT)(ldim * ncell_local),
                              REF_DBL_TYPE),
          "send data");
      ref_free(data);
    }
  }

  if (ref_mpi_once(ref_mpi))
    REIS(next_position, ftell(file), "solb metric record len inconsistent");

  if (ref_mpi_once(ref_mpi)) { /* End */
    keyword_code = 54;
    REIS(1, fwrite(&keyword_code, sizeof(int), 1, file), "end kw");
    next_position = 0;
    RSS(ref_export_meshb_next_position(file, version, next_position), "next p");
  }

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_ngeom(REF_NODE ref_node, REF_GEOM ref_geom,
                                    REF_INT type, REF_INT *ngeom) {
  REF_MPI ref_mpi = ref_node_mpi(ref_node);
  REF_INT geom, node;
  REF_INT ngeom_local;

  ngeom_local = 0;
  each_ref_geom_of(ref_geom, type, geom) {
    node = ref_geom_node(ref_geom, geom);
    if (ref_mpi_rank(ref_mpi) == ref_node_part(ref_node, node)) ngeom_local++;
  }

  RSS(ref_mpi_sum(ref_mpi, &ngeom_local, ngeom, 1, REF_INT_TYPE), "sum");
  RSS(ref_mpi_bcast(ref_mpi, ngeom, 1, REF_INT_TYPE), "bcast");

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_pcd(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char **scalar_names,
                                                const char *filename) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  FILE *file;
  REF_INT i;

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
    fprintf(file, "# .PCD v.7 - Point Cloud Data file format\n");
    fprintf(file, "VERSION .7\n");
    fprintf(file, "FIELDS x y z");
    if (NULL != scalar_names) {
      for (i = 0; i < ldim; i++) fprintf(file, " %s", scalar_names[i]);
    } else {
      for (i = 0; i < ldim; i++) fprintf(file, " V%d", i + 1);
    }
    fprintf(file, "\n");
    fprintf(file, "SIZE");
    for (i = 0; i < 3 + ldim; i++) fprintf(file, " 4");
    fprintf(file, "\n");
    fprintf(file, "TYPE");
    for (i = 0; i < 3 + ldim; i++) fprintf(file, " F");
    fprintf(file, "\n");
    fprintf(file, "COUNT");
    for (i = 0; i < 3 + ldim; i++) fprintf(file, " 1");
    fprintf(file, "\n");
    fprintf(file, "WIDTH " REF_GLOB_FMT "\n", ref_node_n_global(ref_node));
    fprintf(file, "VIEWPOINT 0 0 0 1 0 0 0\n");
    fprintf(file, "POINTS " REF_GLOB_FMT "\n", ref_node_n_global(ref_node));
    fprintf(file, "DATA ascii\n");
  }

  RSS(ref_gather_node_scalar_txt(ref_node, ldim, scalar, " ", REF_TRUE, file),
      "text export");

  if (ref_grid_once(ref_grid)) {
    fclose(file);
  }

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_tec(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char **scalar_names,
                                                const char *filename) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell;
  FILE *file;
  REF_INT i;
  REF_GLOB nnode, *l2c;
  REF_LONG ncell;
  REF_INT min_faceid, max_faceid, cell_id;
  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
    fprintf(file, "title=\"tecplot refine gather\"\n");
    fprintf(file, "variables = \"x\" \"y\" \"z\"");
    if (NULL != scalar_names) {
      for (i = 0; i < ldim; i++) fprintf(file, " \"%s\"", scalar_names[i]);
    } else {
      for (i = 0; i < ldim; i++) fprintf(file, " \"V%d\"", i + 1);
    }
    fprintf(file, "\n");
  }

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  RSS(ref_grid_faceid_range(ref_grid, &min_faceid, &max_faceid), "range");

  for (cell_id = min_faceid; cell_id <= max_faceid; cell_id++) {
    ref_cell = ref_grid_tri(ref_grid);
    RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, cell_id, &nnode,
                                       &ncell, &l2c),
        "l2c");
    if (nnode > 0 && ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        fprintf(file,
                "zone t=\"tri%d\", nodes=" REF_GLOB_FMT
                ", elements=%ld, datapacking=%s, "
                "zonetype=%s\n",
                cell_id, nnode, ncell, "point", "fetriangle");
      }
      RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar, file),
          "nodes");
      RSS(ref_gather_cell_id_tec(ref_node, ref_cell, cell_id, ncell, l2c,
                                 REF_FALSE, file),
          "t");
    }
    ref_free(l2c);

    ref_cell = ref_grid_qua(ref_grid);
    RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, cell_id, &nnode,
                                       &ncell, &l2c),
        "l2c");
    if (nnode > 0 && ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        fprintf(file,
                "zone t=\"quad%d\", nodes=" REF_GLOB_FMT
                ", elements=%ld, datapacking=%s, "
                "zonetype=%s\n",
                cell_id, nnode, ncell, "point", "fequadrilateral");
      }
      RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar, file),
          "nodes");
      RSS(ref_gather_cell_id_tec(ref_node, ref_cell, cell_id, ncell, l2c,
                                 REF_FALSE, file),
          "t");
    }
    ref_free(l2c);
  }

  ref_cell = ref_grid_tet(ref_grid);
  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode > 0 && ncell > 0) {
    if (ref_grid_once(ref_grid)) {
      fprintf(file,
              "zone t=\"tet\", nodes=" REF_GLOB_FMT
              ", elements=%ld, datapacking=%s, "
              "zonetype=%s\n",
              nnode, ncell, "point", "fetetrahedron");
    }
    RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar, file),
        "nodes");
    RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_FALSE, file),
        "t");
  }
  ref_free(l2c);

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_edge_tec(REF_GRID ref_grid,
                                                     REF_INT ldim,
                                                     REF_DBL *scalar,
                                                     const char **scalar_names,
                                                     const char *filename) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell;
  FILE *file;
  REF_INT i;
  REF_GLOB nnode, *l2c;
  REF_LONG ncell;
  REF_INT min_id, max_id, cell_id;
  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
    fprintf(file, "title=\"tecplot refine gather\"\n");
    fprintf(file, "variables = \"x\" \"y\" \"z\"");
    if (NULL != scalar_names) {
      for (i = 0; i < ldim; i++) fprintf(file, " \"%s\"", scalar_names[i]);
    } else {
      for (i = 0; i < ldim; i++) fprintf(file, " \"V%d\"", i + 1);
    }
    fprintf(file, "\n");
  }

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  ref_cell = ref_grid_edg(ref_grid);
  RSS(ref_cell_id_range(ref_cell, ref_mpi, &min_id, &max_id), "range");

  for (cell_id = min_id; cell_id <= max_id; cell_id++) {
    RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, cell_id, &nnode,
                                       &ncell, &l2c),
        "l2c");
    if (nnode > 0 && ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        fprintf(file,
                "zone t=\"edg%d\", nodes=" REF_GLOB_FMT
                ", elements=%ld, datapacking=%s, "
                "zonetype=%s\n",
                cell_id, nnode, ncell, "point", "felineseg");
      }
      RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar, file),
          "nodes");
      RSS(ref_gather_cell_id_tec(ref_node, ref_cell, cell_id, ncell, l2c,
                                 REF_FALSE, file),
          "t");
    }
    ref_free(l2c);
  }

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_scalar_surf_tec(REF_GRID ref_grid, REF_INT ldim,
                                              REF_DBL *scalar,
                                              const char **scalar_names,
                                              const char *filename) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell;
  FILE *file;
  REF_INT i;
  REF_GLOB nnode, *l2c;
  REF_LONG ncell;
  REF_INT min_faceid, max_faceid, cell_id;
  file = NULL;
  if (ref_grid_once(ref_grid)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");
    fprintf(file, "title=\"tecplot refine gather\"\n");
    fprintf(file, "variables = \"x\" \"y\" \"z\"");
    if (NULL != scalar_names) {
      for (i = 0; i < ldim; i++) fprintf(file, " \"%s\"", scalar_names[i]);
    } else {
      for (i = 0; i < ldim; i++) fprintf(file, " \"V%d\"", i + 1);
    }
    fprintf(file, "\n");
  }

  RSS(ref_node_synchronize_globals(ref_node), "sync");

  RSS(ref_grid_faceid_range(ref_grid, &min_faceid, &max_faceid), "range");

  for (cell_id = min_faceid; cell_id <= max_faceid; cell_id++) {
    ref_cell = ref_grid_tri(ref_grid);
    RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, cell_id, &nnode,
                                       &ncell, &l2c),
        "l2c");
    if (nnode > 0 && ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        fprintf(file,
                "zone t=\"tri%d\", nodes=" REF_GLOB_FMT
                ", elements=%ld, datapacking=%s, "
                "zonetype=%s\n",
                cell_id, nnode, ncell, "point", "fetriangle");
      }
      RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar, file),
          "nodes");
      RSS(ref_gather_cell_id_tec(ref_node, ref_cell, cell_id, ncell, l2c,
                                 REF_FALSE, file),
          "t");
    }
    ref_free(l2c);

    ref_cell = ref_grid_qua(ref_grid);
    RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, cell_id, &nnode,
                                       &ncell, &l2c),
        "l2c");
    if (nnode > 0 && ncell > 0) {
      if (ref_grid_once(ref_grid)) {
        fprintf(file,
                "zone t=\"quad%d\", nodes=" REF_GLOB_FMT
                ", elements=%ld, datapacking=%s, "
                "zonetype=%s\n",
                cell_id, nnode, ncell, "point", "fequadrilateral");
      }
      RSS(ref_gather_node_tec_part(ref_node, nnode, l2c, ldim, scalar, file),
          "nodes");
      RSS(ref_gather_cell_id_tec(ref_node, ref_cell, cell_id, ncell, l2c,
                                 REF_FALSE, file),
          "t");
    }
    ref_free(l2c);
  }

  if (ref_grid_once(ref_grid)) fclose(file);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_plt_char_int(const char *char_string, REF_INT max,
                                           REF_INT *n, REF_INT *int_string) {
  REF_INT i;
  *n = 0;
  for (i = 0; i < max; i++) {
    int_string[i] = (REF_INT)char_string[i];
    (*n)++;
    if (0 == int_string[i]) return REF_SUCCESS;
  }
  return REF_INCREASE_LIMIT;
}

REF_FCN static REF_STATUS ref_gather_plt_tri_header(REF_GRID ref_grid,
                                                    REF_INT id, FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_CELL ref_cell = ref_grid_tri(ref_grid);
  char zonename[256];
  int ascii[256];
  int len;
  float zonemarker = 299.0;
  int parentzone = -1;
  int strandid = -1;
  double solutiontime = 0.0;
  int notused = -1;
  int zonetype;
  int datapacking = 0; /*0=Block, point does not work.*/
  int varloc = 0;      /*0 = Don't specify, all data is located at nodes*/
  int faceneighbors = 0;
  int numpts;
  int numelements;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int celldim = 0;
  int aux = 0;

  zonetype = 2; /*2=FETRIANGLE*/

  RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, id, &nnode, &ncell,
                                     &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }

  RAS(nnode <= REF_INT_MAX, "too many nodes for int");
  numpts = (int)nnode;
  RAS(ncell <= REF_INT_MAX, "too many tri for int");
  numelements = (int)ncell;

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    snprintf(zonename, 256, "tri%d", id);
    RSS(ref_gather_plt_char_int(zonename, 256, &len, ascii), "a2i");
    REIS(len, fwrite(&ascii, sizeof(int), (unsigned long)len, file), "title");

    REIS(1, fwrite(&parentzone, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&strandid, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&solutiontime, sizeof(double), 1, file), "double");
    REIS(1, fwrite(&notused, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&zonetype, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&datapacking, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varloc, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&faceneighbors, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numpts, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numelements, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&aux, sizeof(int), 1, file), "int");
  }

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_qua_header(REF_GRID ref_grid,
                                                    REF_INT id, FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_CELL ref_cell = ref_grid_qua(ref_grid);
  char zonename[256];
  int ascii[256];
  int len;
  float zonemarker = 299.0;
  int parentzone = -1;
  int strandid = -1;
  double solutiontime = 0.0;
  int notused = -1;
  int zonetype;
  int datapacking = 0; /*0=Block, point does not work.*/
  int varloc = 0;      /*0 = Don't specify, all data is located at nodes*/
  int faceneighbors = 0;
  int numpts;
  int numelements;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int celldim = 0;
  int aux = 0;

  zonetype = 3; /*3=FEQUADRILATERAL*/

  RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, id, &nnode, &ncell,
                                     &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }

  RAS(nnode <= REF_INT_MAX, "too many nodes for int");
  numpts = (int)nnode;
  RAS(ncell <= REF_INT_MAX, "too many qua for int");
  numelements = (int)ncell;

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    snprintf(zonename, 256, "qua%d", id);
    RSS(ref_gather_plt_char_int(zonename, 256, &len, ascii), "a2i");
    REIS(len, fwrite(&ascii, sizeof(int), (unsigned long)len, file), "title");

    REIS(1, fwrite(&parentzone, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&strandid, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&solutiontime, sizeof(double), 1, file), "double");
    REIS(1, fwrite(&notused, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&zonetype, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&datapacking, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varloc, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&faceneighbors, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numpts, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numelements, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&aux, sizeof(int), 1, file), "int");
  }

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_tet_header(REF_GRID ref_grid,
                                                    FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_CELL ref_cell = ref_grid_tet(ref_grid);
  int ascii[8];
  float zonemarker = 299.0;
  int parentzone = -1;
  int strandid = -1;
  double solutiontime = 0.0;
  int notused = -1;
  int zonetype;
  int datapacking = 0; /*0=Block, point does not work.*/
  int varloc = 0;      /*0 = Don't specify, all data is located at nodes*/
  int faceneighbors = 0;
  int numpts;
  int numelements;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int celldim = 0;
  int aux = 0;

  zonetype = 4; /*4=FETETRAHEDRON*/

  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }

  RAS(nnode <= REF_INT_MAX, "too many nodes for int");
  numpts = (int)nnode;
  RAS(ncell <= REF_INT_MAX, "too many tets for int");
  numelements = (int)ncell;

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    ascii[0] = (int)'e';
    ascii[1] = (int)'4';
    ascii[2] = 0;
    REIS(3, fwrite(&ascii, sizeof(int), 3, file), "title");

    REIS(1, fwrite(&parentzone, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&strandid, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&solutiontime, sizeof(double), 1, file), "double");
    REIS(1, fwrite(&notused, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&zonetype, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&datapacking, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varloc, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&faceneighbors, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numpts, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numelements, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&aux, sizeof(int), 1, file), "int");
  }

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_brick_header(REF_GRID ref_grid,
                                                      REF_CELL ref_cell,
                                                      FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  char zonename[256];
  int ascii[256];
  int len;
  float zonemarker = 299.0;
  int parentzone = -1;
  int strandid = -1;
  double solutiontime = 0.0;
  int notused = -1;
  int zonetype;
  int datapacking = 0; /*0=Block, point does not work.*/
  int varloc = 0;      /*0 = Don't specify, all data is located at nodes*/
  int faceneighbors = 0;
  int numpts;
  int numelements;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int celldim = 0;
  int aux = 0;

  zonetype = 5; /*5=FEBRICK*/

  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }

  RAS(nnode <= REF_INT_MAX, "too many nodes for int");
  numpts = (int)nnode;
  RAS(ncell <= REF_INT_MAX, "too many bricks for int");
  numelements = (int)ncell;

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    snprintf(zonename, 256, "brick%d", ref_cell_node_per(ref_cell));
    RSS(ref_gather_plt_char_int(zonename, 256, &len, ascii), "a2i");
    REIS(len, fwrite(&ascii, sizeof(int), (unsigned long)len, file), "title");

    REIS(1, fwrite(&parentzone, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&strandid, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&solutiontime, sizeof(double), 1, file), "double");
    REIS(1, fwrite(&notused, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&zonetype, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&datapacking, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varloc, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&faceneighbors, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numpts, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&numelements, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&celldim, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&aux, sizeof(int), 1, file), "int");
  }

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_tri_zone(REF_GRID ref_grid, REF_INT id,
                                                  REF_INT ldim, REF_DBL *scalar,
                                                  FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell = ref_grid_tri(ref_grid);
  float zonemarker = 299.0;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int dataformat = 2; /*1=Float, 2=Double*/
  int passive = 0;
  int varsharing = 0;
  int connsharing = -1;
  double mindata, maxdata, tempdata;
  REF_INT node, ixyz, i;

  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tri start");

  RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, id, &nnode, &ncell,
                                     &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tri compact");

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    for (i = 0; i < 3 + ldim; i++) {
      REIS(1, fwrite(&dataformat, sizeof(int), 1, file), "int");
    }

    REIS(1, fwrite(&passive, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varsharing, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&connsharing, sizeof(int), 1, file), "int");
  }
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tri header");

  for (ixyz = 0; ixyz < 3; ixyz++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, ref_node_xyz(ref_node, ixyz, node));
        maxdata = MAX(maxdata, ref_node_xyz(ref_node, ixyz, node));
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }
  for (i = 0; i < ldim; i++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, scalar[i + ldim * node]);
        maxdata = MAX(maxdata, scalar[i + ldim * node]);
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tri minmax");

  RSS(ref_gather_node_tec_block(ref_node, nnode, l2c, ldim, scalar, dataformat,
                                file),
      "block points");
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tri node");

  RSS(ref_gather_cell_id_tec(ref_node, ref_cell, id, ncell, l2c, REF_TRUE,
                             file),
      "c2n");
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tri cell");

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_qua_zone(REF_GRID ref_grid, REF_INT id,
                                                  REF_INT ldim, REF_DBL *scalar,
                                                  FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell = ref_grid_qua(ref_grid);
  float zonemarker = 299.0;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int dataformat = 2; /*1=Float, 2=Double*/
  int passive = 0;
  int varsharing = 0;
  int connsharing = -1;
  double mindata, maxdata, tempdata;
  REF_INT node, ixyz, i;

  RSS(ref_grid_compact_cell_id_nodes(ref_grid, ref_cell, id, &nnode, &ncell,
                                     &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    for (i = 0; i < 3 + ldim; i++) {
      REIS(1, fwrite(&dataformat, sizeof(int), 1, file), "int");
    }

    REIS(1, fwrite(&passive, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varsharing, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&connsharing, sizeof(int), 1, file), "int");
  }

  for (ixyz = 0; ixyz < 3; ixyz++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, ref_node_xyz(ref_node, ixyz, node));
        maxdata = MAX(maxdata, ref_node_xyz(ref_node, ixyz, node));
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }
  for (i = 0; i < ldim; i++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, scalar[i + ldim * node]);
        maxdata = MAX(maxdata, scalar[i + ldim * node]);
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }

  RSS(ref_gather_node_tec_block(ref_node, nnode, l2c, ldim, scalar, dataformat,
                                file),
      "block points");

  RSS(ref_gather_cell_id_tec(ref_node, ref_cell, id, ncell, l2c, REF_TRUE,
                             file),
      "c2n");

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_tet_zone(REF_GRID ref_grid,
                                                  REF_INT ldim, REF_DBL *scalar,
                                                  FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell = ref_grid_tet(ref_grid);
  float zonemarker = 299.0;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int dataformat = 2; /*1=Float, 2=Double*/
  int passive = 0;
  int varsharing = 0;
  int connsharing = -1;
  double mindata, maxdata, tempdata;
  REF_INT node, ixyz, i;

  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tet start");

  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tet compact");

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    for (i = 0; i < 3 + ldim; i++) {
      REIS(1, fwrite(&dataformat, sizeof(int), 1, file), "int");
    }

    REIS(1, fwrite(&passive, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varsharing, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&connsharing, sizeof(int), 1, file), "int");
  }

  for (ixyz = 0; ixyz < 3; ixyz++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, ref_node_xyz(ref_node, ixyz, node));
        maxdata = MAX(maxdata, ref_node_xyz(ref_node, ixyz, node));
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }
  for (i = 0; i < ldim; i++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, scalar[i + ldim * node]);
        maxdata = MAX(maxdata, scalar[i + ldim * node]);
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tet min/max");

  RSS(ref_gather_node_tec_block(ref_node, nnode, l2c, ldim, scalar, dataformat,
                                file),
      "block points");
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tet node");

  RSS(ref_gather_cell_tec(ref_node, ref_cell, ncell, l2c, REF_TRUE, file),
      "c2n");
  if (1 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt tet cell");

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_plt_brick_zone(REF_GRID ref_grid,
                                                    REF_CELL ref_cell,
                                                    REF_INT ldim,
                                                    REF_DBL *scalar,
                                                    FILE *file) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  float zonemarker = 299.0;
  REF_LONG ncell;
  REF_GLOB nnode, *l2c;
  int dataformat = 2; /*1=Float, 2=Double*/
  int passive = 0;
  int varsharing = 0;
  int connsharing = -1;
  double mindata, maxdata, tempdata;
  REF_INT node, ixyz, i;

  RSS(ref_grid_compact_cell_nodes(ref_grid, ref_cell, &nnode, &ncell, &l2c),
      "l2c");
  if (nnode <= 0 || ncell <= 0) {
    ref_free(l2c);
    return REF_SUCCESS;
  }

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&zonemarker, sizeof(float), 1, file), "zonemarker");

    for (i = 0; i < 3 + ldim; i++) {
      REIS(1, fwrite(&dataformat, sizeof(int), 1, file), "int");
    }

    REIS(1, fwrite(&passive, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&varsharing, sizeof(int), 1, file), "int");
    REIS(1, fwrite(&connsharing, sizeof(int), 1, file), "int");
  }

  for (ixyz = 0; ixyz < 3; ixyz++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, ref_node_xyz(ref_node, ixyz, node));
        maxdata = MAX(maxdata, ref_node_xyz(ref_node, ixyz, node));
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }
  for (i = 0; i < ldim; i++) {
    mindata = REF_DBL_MAX;
    maxdata = REF_DBL_MIN;
    for (node = 0; node < ref_node_max(ref_node); node++) {
      if (REF_EMPTY != l2c[node] && ref_node_owned(ref_node, node)) {
        mindata = MIN(mindata, scalar[i + ldim * node]);
        maxdata = MAX(maxdata, scalar[i + ldim * node]);
      }
    }
    tempdata = mindata;
    RSS(ref_mpi_min(ref_mpi, &tempdata, &mindata, REF_DBL_TYPE), "mpi min");
    tempdata = maxdata;
    RSS(ref_mpi_max(ref_mpi, &tempdata, &maxdata, REF_DBL_TYPE), "mpi max");
    if (ref_mpi_once(ref_mpi)) {
      REIS(1, fwrite(&mindata, sizeof(double), 1, file), "mindata");
      REIS(1, fwrite(&maxdata, sizeof(double), 1, file), "maxdata");
    }
  }

  RSS(ref_gather_node_tec_block(ref_node, nnode, l2c, ldim, scalar, dataformat,
                                file),
      "block points");

  RSS(ref_gather_brick_tec(ref_node, ref_cell, ncell, l2c, REF_TRUE, file),
      "c2n");

  ref_free(l2c);
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_gather_scalar_plt(REF_GRID ref_grid, REF_INT ldim,
                                                REF_DBL *scalar,
                                                const char **scalar_names,
                                                REF_BOOL as_brick,
                                                const char *filename) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  FILE *file = NULL;
  int one = 1;
  int filetype = 0;
  int ascii[1024];
  int i, len, numvar = 3 + ldim;
  float eohmarker = 357.0;
  REF_INT cell_id, min_faceid, max_faceid;

  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "reset timing");

  RSS(ref_node_synchronize_globals(ref_node), "sync");
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "header sync global");

  if (ref_mpi_once(ref_mpi)) {
    file = fopen(filename, "w");
    if (NULL == (void *)file) printf("unable to open %s\n", filename);
    RNS(file, "unable to open file");

    REIS(8, fwrite(&"#!TDV112", sizeof(char), 8, file), "header");
    REIS(1, fwrite(&one, sizeof(int), 1, file), "magic");
    REIS(1, fwrite(&filetype, sizeof(int), 1, file), "filetype");

    ascii[0] = (int)'f';
    ascii[1] = (int)'t';
    ascii[2] = 0;
    REIS(3, fwrite(&ascii, sizeof(int), 3, file), "title");

    REIS(1, fwrite(&numvar, sizeof(int), 1, file), "numvar");
    ascii[0] = (int)'x';
    ascii[1] = 0;
    REIS(2, fwrite(&ascii, sizeof(int), 2, file), "var");
    ascii[0] = (int)'y';
    ascii[1] = 0;
    REIS(2, fwrite(&ascii, sizeof(int), 2, file), "var");
    ascii[0] = (int)'z';
    ascii[1] = 0;
    REIS(2, fwrite(&ascii, sizeof(int), 2, file), "var");
    for (i = 0; i < ldim; i++) {
      len = 0;
      if (NULL == scalar_names) {
        char default_name[1000];
        snprintf(default_name, 1000, "V%d", i + 1);
        RSS(ref_gather_plt_char_int(default_name, 1024, &len, ascii), "a2i");
      } else {
        RSS(ref_gather_plt_char_int(scalar_names[i], 1024, &len, ascii), "a2i");
      }
      REIS(len, fwrite(&ascii, sizeof(int), (unsigned long)len, file), "var");
    }
  }

  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "header vars");
  RSS(ref_grid_faceid_range(ref_grid, &min_faceid, &max_faceid), "range");
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "header faceid range");

  for (cell_id = min_faceid; cell_id <= max_faceid; cell_id++) {
    RSS(ref_gather_plt_tri_header(ref_grid, cell_id, file), "plt tri header");
    RSS(ref_gather_plt_qua_header(ref_grid, cell_id, file), "plt qua header");
  }
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "header surf");
  if (as_brick) {
    RSS(ref_gather_plt_brick_header(ref_grid, ref_grid_tet(ref_grid), file),
        "plt tet brick header");
  } else {
    RSS(ref_gather_plt_tet_header(ref_grid, file), "plt tet header");
  }
  RSS(ref_gather_plt_brick_header(ref_grid, ref_grid_pyr(ref_grid), file),
      "plt pyr brick header");
  RSS(ref_gather_plt_brick_header(ref_grid, ref_grid_pri(ref_grid), file),
      "plt pri brick header");
  RSS(ref_gather_plt_brick_header(ref_grid, ref_grid_hex(ref_grid), file),
      "plt hex brick header");
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "header vol");

  if (ref_mpi_once(ref_mpi)) {
    REIS(1, fwrite(&eohmarker, sizeof(float), 1, file), "eohmarker");
  }
  if (0 < ref_mpi_timing(ref_mpi))
    ref_mpi_stopwatch_stop(ref_mpi, "plt end of header");

  for (cell_id = min_faceid; cell_id <= max_faceid; cell_id++) {
    RSS(ref_gather_plt_tri_zone(ref_grid, cell_id, ldim, scalar, file),
        "plt tri zone");
    RSS(ref_gather_plt_qua_zone(ref_grid, cell_id, ldim, scalar, file),
        "plt qua zone");
  }
  if (0 < ref_mpi_timing(ref_mpi)) ref_mpi_stopwatch_stop(ref_mpi, "surf zone");

  if (as_brick) {
    RSS(ref_gather_plt_brick_zone(ref_grid, ref_grid_tet(ref_grid), ldim,
                                  scalar, file),
        "plt tet brick zone");
  } else {
    RSS(ref_gather_plt_tet_zone(ref_grid, ldim, scalar, file), "surf zone");
  }
  RSS(ref_gather_plt_brick_zone(ref_grid, ref_grid_pyr(ref_grid), ldim, scalar,
                                file),
      "plt pyr brick zone");
  RSS(ref_gather_plt_brick_zone(ref_grid, ref_grid_pri(ref_grid), ldim, scalar,
                                file),
      "plt pri brick zone");
  RSS(ref_gather_plt_brick_zone(ref_grid, ref_grid_hex(ref_grid), ldim, scalar,
                                file),
      "plt hex brick zone");
  if (0 < ref_mpi_timing(ref_mpi)) ref_mpi_stopwatch_stop(ref_mpi, "vol zone");

  if (ref_mpi_once(ref_mpi)) {
    fclose(file);
  }

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_scalar_by_extension(REF_GRID ref_grid,
                                                  REF_INT ldim, REF_DBL *scalar,
                                                  const char **scalar_names,
                                                  const char *filename) {
  size_t end_of_string;

  end_of_string = strlen(filename);

  if (end_of_string > 9 &&
      strcmp(&filename[end_of_string - 9], "-edge.tec") == 0) {
    RSS(ref_gather_scalar_edge_tec(ref_grid, ldim, scalar, scalar_names,
                                   filename),
        "scalar edge tec");
    return REF_SUCCESS;
  }
  if (end_of_string > 10 &&
      strcmp(&filename[end_of_string - 10], "-brick.plt") == 0) {
    RSS(ref_gather_scalar_plt(ref_grid, ldim, scalar, scalar_names, REF_TRUE,
                              filename),
        "scalar tec");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".plt") == 0) {
    RSS(ref_gather_scalar_plt(ref_grid, ldim, scalar, scalar_names, REF_FALSE,
                              filename),
        "scalar tec");
    return REF_SUCCESS;
  }
  if ((end_of_string > 4 &&
       strcmp(&filename[end_of_string - 4], ".tec") == 0) ||
      (end_of_string > 4 &&
       strcmp(&filename[end_of_string - 4], ".dat") == 0) ||
      (end_of_string > 2 && strcmp(&filename[end_of_string - 2], ".t") == 0)) {
    RSS(ref_gather_scalar_tec(ref_grid, ldim, scalar, scalar_names, filename),
        "scalar tec");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".pcd") == 0) {
    RSS(ref_gather_scalar_pcd(ref_grid, ldim, scalar, scalar_names, filename),
        "scalar pcd");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".rst") == 0) {
    RSS(ref_gather_scalar_rst(ref_grid, ldim, scalar, filename), "scalar rst");
    return REF_SUCCESS;
  }
  if (end_of_string > 12 &&
      strcmp(&filename[end_of_string - 12], ".restart_sol") == 0) {
    RSS(ref_gather_scalar_cell_restart_sol(ref_grid, ldim, scalar, filename),
        "scalar sol");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".sol") == 0) {
    RSS(ref_gather_scalar_sol(ref_grid, ldim, scalar, filename), "scalar sol");
    return REF_SUCCESS;
  }
  if (end_of_string > 15 &&
      strcmp(&filename[end_of_string - 15], "-usm3dcell.solb") == 0) {
    RSS(ref_gather_scalar_cell_solb(ref_grid, ldim, scalar, filename),
        "scalar usm3d cell solb");
    return REF_SUCCESS;
  }
  if (end_of_string > 5 && strcmp(&filename[end_of_string - 5], ".solb") == 0) {
    RSS(ref_gather_scalar_solb(ref_grid, ldim, scalar, filename),
        "scalar solb");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".bin") == 0) {
    RSS(ref_gather_scalar_bin(ref_grid, ldim, scalar, filename), "scalar bin");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".txt") == 0) {
    RSS(ref_gather_scalar_txt(ref_grid, ldim, scalar, " ", filename),
        "scalar txt");
    return REF_SUCCESS;
  }
  if (end_of_string > 4 && strcmp(&filename[end_of_string - 4], ".csv") == 0) {
    RSS(ref_gather_scalar_txt(ref_grid, ldim, scalar, ",", filename),
        "scalar txt");
    return REF_SUCCESS;
  }
  printf("%s: %d: %s %s\n", __FILE__, __LINE__,
         "input file name extension unknown", filename);
  return REF_FAILURE;
}

REF_FCN REF_STATUS ref_gather_surf_status_tec(REF_GRID ref_grid,
                                              const char *filename) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_CELL ref_cell;
  REF_DBL *scalar, quality, normdev;
  REF_INT cell, edge, node0, node1, cell_node, nodes[REF_CELL_MAX_SIZE_PER];
  REF_EDGE ref_edge;
  REF_DBL edge_ratio;
  const char *vars[4];
  REF_INT ldim = 4;

  vars[0] = "q";
  vars[1] = "s";
  vars[2] = "l";
  vars[3] = "n";
  ref_malloc_init(scalar, ldim * ref_node_max(ref_node), REF_DBL, 1.0);
  ref_cell = ref_grid_tri(ref_grid);
  each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
    RSS(ref_node_tri_quality(ref_node, nodes, &quality), "tri qual");
    normdev = 2.0;
    if (ref_geom_model_loaded(ref_grid_geom(ref_grid)) ||
        ref_geom_meshlinked(ref_grid_geom(ref_grid))) {
      RSS(ref_geom_tri_norm_deviation(ref_grid, nodes, &normdev), "norm dev");
    }
    each_ref_cell_cell_node(ref_cell, cell_node) {
      scalar[0 + ldim * nodes[cell_node]] =
          MIN(scalar[0 + ldim * nodes[cell_node]], quality);
      scalar[3 + ldim * nodes[cell_node]] =
          MIN(scalar[3 + ldim * nodes[cell_node]], normdev);
    }
  }
  RSS(ref_edge_create(&ref_edge, ref_grid), "create edges");
  for (edge = 0; edge < ref_edge_n(ref_edge); edge++) {
    node0 = ref_edge_e2n(ref_edge, 0, edge);
    node1 = ref_edge_e2n(ref_edge, 1, edge);
    RSS(ref_node_ratio(ref_node, node0, node1, &edge_ratio), "ratio");
    scalar[1 + ldim * node0] = MIN(scalar[1 + ldim * node0], edge_ratio);
    scalar[1 + ldim * node1] = MIN(scalar[1 + ldim * node1], edge_ratio);
    scalar[2 + ldim * node0] = MAX(scalar[2 + ldim * node0], edge_ratio);
    scalar[2 + ldim * node1] = MAX(scalar[2 + ldim * node1], edge_ratio);
  }
  RSS(ref_edge_free(ref_edge), "free edges");

  RSS(ref_gather_scalar_surf_tec(ref_grid, ldim, scalar, vars, filename),
      "dump");

  ref_free(scalar);

  return REF_SUCCESS;
}

REF_FCN REF_STATUS ref_gather_volume_status_tec(REF_GRID ref_grid,
                                                const char *filename) {
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_INT edge, node0, node1;
  REF_EDGE ref_edge;
  REF_DBL *scalar, edge_ratio;
  const char *vars[2];
  REF_INT ldim = 2;

  vars[0] = "s";
  vars[1] = "l";
  ref_malloc_init(scalar, ldim * ref_node_max(ref_node), REF_DBL, 1.0);
  RSS(ref_edge_create(&ref_edge, ref_grid), "create edges");
  for (edge = 0; edge < ref_edge_n(ref_edge); edge++) {
    node0 = ref_edge_e2n(ref_edge, 0, edge);
    node1 = ref_edge_e2n(ref_edge, 1, edge);
    RSS(ref_node_ratio(ref_node, node0, node1, &edge_ratio), "ratio");
    scalar[0 + ldim * node0] = MIN(scalar[0 + ldim * node0], edge_ratio);
    scalar[0 + ldim * node1] = MIN(scalar[0 + ldim * node1], edge_ratio);
    scalar[1 + ldim * node0] = MAX(scalar[1 + ldim * node0], edge_ratio);
    scalar[1 + ldim * node1] = MAX(scalar[1 + ldim * node1], edge_ratio);
  }
  RSS(ref_edge_free(ref_edge), "free edges");

  RSS(ref_gather_scalar_by_extension(ref_grid, ldim, scalar, vars, filename),
      "dump");

  ref_free(scalar);

  return REF_SUCCESS;
}

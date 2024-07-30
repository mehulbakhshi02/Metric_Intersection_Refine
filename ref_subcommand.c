
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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref_adapt.h"
#include "ref_args.h"
#include "ref_axi.h"
#include "ref_defs.h"
#include "ref_dist.h"
#include "ref_egads.h"
#include "ref_export.h"
#include "ref_gather.h"
#include "ref_geom.h"
#include "ref_grid.h"
#include "ref_import.h"
#include "ref_inflate.h"
#include "ref_iso.h"
#include "ref_layer.h"
#include "ref_malloc.h"
#include "ref_math.h"
#include "ref_matrix.h"
#include "ref_meshlink.h"
#include "ref_metric.h"
#include "ref_mpi.h"
#include "ref_part.h"
#include "ref_phys.h"
#include "ref_shard.h"
#include "ref_sort.h"
#include "ref_split.h"
#include "ref_subdiv.h"
#include "ref_validation.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#ifdef REFINE_VERSION
#define VERSION REFINE_VERSION
#else
#define VERSION "not available"
#endif
#endif

static void usage(const char *name) {
  printf("usage: \n %s [--help] <subcommand> [<args>]\n", name);
  printf("\n");
  printf("ref subcommands:\n");
  printf("  adapt        Adapt a mesh\n");
  printf("  bootstrap    Create initial mesh from EGADS file\n");
  printf("  collar       Inflate surface to create swept mesh\n");
  printf("  distance     Calculate wall distance (for turbulence model)\n");
  printf("  examine      Report mesh or solution file meta data.\n");
  /*printf("  grow         Fills surface mesh with volume to debug
   * bootstrap\n");*/
  printf("  interpolate  Interpolate a field from one mesh to another\n");
  printf("  loop         Multiscale metric, adapt, and interpolation.\n");
  printf("  multiscale   Compute a multiscale metric.\n");
  /*printf("  node       Reports location of a node by index\n");*/
  /*printf("  quilt      Construct effective EGADS model.\n");*/
  printf("  surface      depreciated, see translate ... --surface.\n");
  printf("  translate    Convert mesh formats.\n");
  printf("  visualize    Convert solution formats.\n");
  printf("  with2matrix  Intersection of matrices.\n");
  printf("\n");
  printf("'ref <command> -h' provides details on a specific subcommand.\n");
}

static void option_uniform_help(void) {
  printf(
      "  --uniform box {ceil,floor} h0 decay_distance xmin ymin zmin "
      "xmax ymax zmax\n");
  printf(
      "  --uniform cyl {ceil,floor} h0 decay_distance x1 y1 z1 "
      "x2 y2 z2 r1 r2\n");
  printf("      decay_distance is negative to increase h with distance.\n");
  printf("      decay_distance is positive to decrease h with distance.\n");
}

static void option_auto_tprarms_help(void) {
  printf("  --auto-tparams {or combination of options} adjust .tParams\n");
  printf("        1:single edge, 2:chord violation, 4:face width (-1:all)\n");
}

static void adapt_help(const char *name) {
  printf("usage: \n %s adapt input_mesh.extension [<options>]\n", name);
  printf("  -x  output_mesh.extension\n");
  printf("  --metric <metric.solb> (geometry feature metric when missing)\n");
  printf("  --egads <geometry.egads> (ignored with EGADSlite)\n");
  printf("  --implied-complexity [complexity] imply metric from input mesh\n");
  printf("      and scale to complexity\n");
  printf("  --spalding [y+=1] [complexity]\n");
  printf("      construct a multiscale metric to control interpolation\n");
  printf("      error in u+ of Spalding's Law. Requires boundary conditions\n");
  printf("      via the --fun3d-mapbc or --viscous-tags options.\n");
  printf("  --stepexp [h0] [h1] [h2] [s1] [s2] [width]\n");
  printf("      construct an isotropic metric of constant then exponential\n");
  printf("      Requires boundary conditions via the --fun3d-mapbc or\n");
  printf("      --viscous-tags options.\n");
  option_uniform_help();
  printf("  --fun3d-mapbc fun3d_format.mapbc\n");
  printf("  --viscous-tags <comma-separated list of viscous boundary tags>\n");
  printf("  --axi forms an extruded wedge from 2D mesh.\n");
  printf("  --partitioner selects domain decomposition method.\n");
  printf("      2: ParMETIS graph partitioning.\n");
  printf("      3: Zoltan graph partitioning.\n");
  printf("      4: Zoltan recursive bisection.\n");
  printf("      5: native recursive bisection.\n");
  printf("\n");
}
static void collar_help(const char *name) {
  printf(
      "usage: \n %s collar method core_mesh.ext "
      "nlayers first_thickness total_thickness mach\n",
      name);
  printf("  where method is:\n");
  printf("    <n>ormal extrusion normal to polygonal prism face\n");
  printf("    <f>lat extrusion of interpolated face edges\n");
  printf("    <r>adial extrusion from origin (not guarenteed)\n");
  printf("  --usm3d-mapbc usm3d_format.mapbc family_name bc_type\n");
  printf("  --fun3d-mapbc fun3d_format.mapbc (requires 'inflate' family)\n");
  printf("  --rotate angle_in_degrees (applied before inflation)\n");
  printf("  --origin ox oy oz (default is 0 0 zmid)\n");
  printf("  -x output_mesh.extension\n");
  printf("\n");
}
static void bootstrap_help(const char *name) {
  printf("usage: \n %s bootstrap project.egads [-t]\n", name);
  printf("  -t  tecplot movie of surface curvature adaptation\n");
  printf("        in files ref_gather_movie.tec and ref_gather_histo.tec\n");
  printf("  --mesher {tetgen|aflr} volume mesher\n");
  printf("  --mesher-options \"<options>\" quoted mesher options.\n");
  option_auto_tprarms_help();
  printf("  --axi sets 6022 boundary condition for extruded wedge 2D.\n");
  printf("\n");
}
static void distance_help(const char *name) {
  printf("usage: \n %s distance input_mesh.extension distance.solb\n", name);
  printf("  --fun3d-mapbc fun3d_format.mapbc\n");
  printf("  --viscous-tags <comma-separated list of viscous boundary tags>\n");
  printf("\n");
}
static void examine_help(const char *name) {
  printf("usage: \n %s examine input_mesh_or_solb.extension\n", name);
  printf("\n");
}
static void grow_help(const char *name) {
  printf("usage: \n %s grow surface.meshb volume.meshb\n", name);
  printf("  --mesher {tetgen|aflr} volume mesher\n");
  printf("  --mesher-options \"<options>\" quoted mesher options.\n");
  printf("\n");
}
static void interpolate_help(const char *name) {
  printf(
      "usage: \n %s interpolate donor.meshb donor.solb receptor.meshb "
      "receptor.solb\n",
      name);
  printf("\n");
  printf("  options:\n");
  printf("   --extrude receptor.solb data to two planes.\n");
  printf("   --face <face id> <persist>.solb\n");
  printf("       where persist.solb is copied to receptor.solb\n");
  printf("       and face id is replaced with donor.solb.\n");
  printf("\n");
}

static void loop_help(const char *name) {
  printf(
      "usage: \n %s loop <input_project_name> <output_project_name>"
      " complexity [<options>]\n",
      name);
  printf("\n");
  printf("  expects:\n");
  printf(
      "   <input_project_name>.meshb is"
      " mesh with geometry association and model.\n");
  printf(
      "   <input_project_name>_volume.solb is"
      " [rho,u,v,w,p] or [rho,u,v,w,p,turb1]\n");
  printf("    in FUN3D nondimensionalization.\n");
  printf("   complexity is half of the target number of vertices.\n");
  printf("\n");
  printf("  creates:\n");
  printf(
      "   <output_project_name>.meshb is"
      " mesh with geometry association and model.\n");
  printf(
      "   <output_project_name>.lb8.ugrid is"
      " FUN3D compatible little-endian mesh.\n");
  printf(
      "   <output_project_name>-restart.solb is"
      " an interpolated solution.\n");
  printf("\n");
  printf("  options:\n");
  printf("   --egads <geometry.egads> (ignored with EGADSlite)\n");
  printf("   --norm-power <power> multiscale metric norm power.\n");
  printf("       Default power is 2 (1 for goal-based metrics)\n");
  printf("   --gradation <gradation> (default -1)\n");
  printf("       positive: metric-space gradation stretching ratio.\n");
  printf("       negative: mixed-space gradation.\n");
  printf("   --partitioner <id> selects domain decomposition method.\n");
  printf("       2: ParMETIS graph partitioning.\n");
  printf("       3: Zoltan graph partitioning.\n");
  printf("       4: Zoltan recursive bisection.\n");
  printf("       5: native recursive bisection.\n");
  printf("   --mesh-extension <output mesh extension> (replaces lb8.ugrid).\n");
  printf("   --fixed-point <middle-string> \\\n");
  printf("       <first_timestep> <timestep_increment> <last_timestep>\n");
  printf("       where <input_project_name><middle-string>N.solb are\n");
  printf("       scalar fields and N is the timestep index.\n");
  printf("   --ddes <Mach> <Reynolds number>\n");
  printf("       requires --fixed-point and --fun3d-mapbc/--viscous-tags\n");
  printf(
      "       for computing distance function. LES AR set by --aspect-ratio\n");
  printf("   --aspect-ratio <aspect ratio limit>.\n");
  printf("       where default LES AR is 1.\n");
  printf("   --interpolant <type or file.solb> multiscale scalar field.\n");
  printf(
      "       Type is mach (default), "
      "incomp (incompressible vel magnitude),\n");
  printf("       htot, ptot, pressure, density, or temperature.\n");
  printf("       Read from file.solb, if not a recognized type.\n");
  printf("   --export-metric writes <input_project_name>-metric.solb.\n");
  printf("   --opt-goal metric of Loseille et al. AIAA 2007--4186.\n");
  printf("        Include flow and adjoint information in volume.solb.\n");
  printf("        Use --fun3d-mapbc or --viscous-tags with strong BCs.\n");
  printf("   --cons-visc <mach> <re> <temperature> see AIAA 2019--2947.\n");
  printf("        <mach> is reference Mach nubmer.\n");
  printf("        <re> is reference Reylonds number in grid units.\n");
  printf("        <temperature> is reference temperature in K.\n");
  printf("        Include flow and adjoint information in volume.solb.\n");
  printf("        Use --fun3d-mapbc or --viscous-tags with strong BCs.\n");
  printf("  --fun3d-mapbc fun3d_format.mapbc\n");
  printf("  --viscous-tags <comma-separated list of viscous boundary tags>\n");
  printf("  --deforming mesh flow solve, include xyz in *_volume.solb.\n");
  printf("  --mixed implies multiscale metric from mixed elements.\n");
  printf("  --axi forms an extruded wedge from 2D mesh.\n");
  printf("  --buffer coarsens the metric approaching the x max boundary.\n");
  option_uniform_help();

  printf("\n");
}
static void multiscale_help(const char *name) {
  printf(
      "usage: \n %s multiscale input_mesh.extension scalar.{solb,snap} "
      "complexity metric.solb\n",
      name);
  printf("   complexity is approximately half the target number of vertices\n");
  printf("\n");
  printf("  options:\n");
  printf("   --norm-power <power> multiscale metric norm power (default 2)\n");
  printf("   --gradation <gradation> (default -1)\n");
  printf("       positive: metric-space gradation stretching ratio.\n");
  printf("       negative: mixed-space gradation.\n");
  printf("   --buffer coarsens the metric approaching the x max boundary.\n");
  option_uniform_help();
  printf("   --hessian expects hessian.* in place of scalar.{solb,snap}.\n");
  printf("   --pcd <project.pcd> exports isotropic spacing point cloud.\n");
  printf("   --combine <scalar2.solb> <scalar2 ratio>.\n");
  printf("   --aspect-ratio <aspect ratio limit>.\n");
  printf("\n");
}
static void node_help(const char *name) {
  printf("usage: \n %s node input.meshb node_index node_index ...\n", name);
  printf("  node_index is zero-based\n");
  printf("\n");
}
static void quilt_help(const char *name) {
  printf("usage: \n %s quilt original.egads\n", name);
  printf("  originaleff.egads is output EGADS model with EBODY\n");
  option_auto_tprarms_help();
  printf("\n");
}
static void translate_help(const char *name) {
  printf("usage: \n %s translate input_mesh.extension output_mesh.extension \n",
         name);
  printf("\n");
  printf("  options:\n");
  printf("   --scale <scale> scales vertex locations about origin.\n");
  printf("   --shift <dx> <dy> <dz> shift vertex locations.\n");
  printf("   --rotatey <deg> rotate vertex locations about (0,0,0).\n");
  printf("   --surface extracts surface elements (deletes volume).\n");
  printf("   --enrich2 promotes elements to Q2.\n");
  printf("   --shard converts mixed-elments to simplicies.\n");
  printf("   --extrude a 2D mesh to single layer of prisms.\n");
  printf("       extrusion added implicitly for ugrid output files\n");
  printf("   --planes <N> extrude a 2D mesh to N layers of prisms.\n");
  printf("   --zero-y-face <face id> explicitly set y=0 on face id.\n");
  printf("   --axi convert an extruded mesh into a wedge at z=y=0 axis\n");
  printf("\n");
}
static void visualize_help(const char *name) {
  printf(
      "usage: \n %s visualize input_mesh.extension input_solution.extension "
      "output_solution.extension\n",
      name);
  printf("\n");
  printf(
      "  input_solution.extension or output_solution.extension "
      "can be 'none'.\n  input_solution.extension can be 'degree'.\n");
  printf("  options:\n");
  printf("   --surface extracts surface elements (deletes volume).\n");
  printf(
      "   --subtract <baseline_solution.extension> "
      "computes (input-baseline).\n");
  printf(
      "   --iso <0-based variable index> <threshold> <iso.extension> "
      "extracts an isosurface.\n");
  printf(
      "   --slice <nx> <ny> <nz> <offset> <slice.extension> "
      "extracts a slice.\n");
  printf(
      "   --boomray <x0> <y0> <z0> <x1> <y1> <z1> <ray.tec> "
      "extracts a ray\n      of dp/pinf defined by two points.\n");

  printf("\n");
}

static void with2matrix_help(const char *name) {
  printf("Used for metric intersection\n");
  printf("Usage: \n %s with2matrix grid.ext metric0.solb metric1.solb "
         "output-metric.solb\n", name);

  printf("\n");
}

static REF_STATUS with2matrix(REF_MPI ref_mpi, int argc, char *argv[]) {
    
    REF_INT intersection_pos = 1;
    REF_GRID ref_grid;
    REF_DBL *metric0, *metric1, *metric;
    REF_INT node;

    REIS(1, intersection_pos,
         "required args: with2matrix grid.ext metric0.solb metric1.solb "
         "output-metric.solb");
    REIS(6, argc,
         "required args: with2matrix grid.ext metric0.solb metric1.solb "
         "output-metric.solb");
    if (ref_mpi_once(ref_mpi)) printf("reading grid %s\n", argv[2]);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, argv[2]),
        "unable to load grid in position 2");

    if (ref_mpi_once(ref_mpi)) printf("reading metric0 %s\n", argv[3]);
    RSS(ref_part_metric(ref_grid_node(ref_grid), argv[3]),
        "unable to load metric in position 3");
    ref_malloc(metric0, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    RSS(ref_metric_from_node(metric0, ref_grid_node(ref_grid)), "get m0");

    if (ref_mpi_once(ref_mpi)) printf("reading metric1 %s\n", argv[4]);
    RSS(ref_part_metric(ref_grid_node(ref_grid), argv[4]),
        "unable to load metric in position 4");
    ref_malloc(metric1, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    RSS(ref_metric_from_node(metric1, ref_grid_node(ref_grid)), "get m1");

    ref_malloc(metric, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      RSS(ref_matrix_intersect(&(metric0[6 * node]), &(metric1[6 * node]),
                               &(metric[6 * node])),
          "intersect");
    }
    RSS(ref_metric_to_node(metric, ref_grid_node(ref_grid)), "set node");
    ref_free(metric);
    ref_free(metric1);
    ref_free(metric0);

    if (ref_mpi_once(ref_grid_mpi(ref_grid)))
      printf("writing metric %s\n", argv[5]);
    RSS(ref_gather_metric(ref_grid, argv[5]), "export scaled metric");

    RSS(ref_grid_free(ref_grid), "free");
    RSS(ref_mpi_free(ref_mpi), "free");
    RSS(ref_mpi_stop(), "stop");
    return 0;
}

int mehul_iter=0;

static REF_STATUS spalding_metric(REF_GRID ref_grid, REF_DICT ref_dict_bcs,
                                  REF_DBL spalding_yplus, REF_DBL complexity,
                                  int argc, char *argv[]) {
  char *out_metric;
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_DBL *metric;
  REF_DBL *distance, *uplus, yplus;
  REF_INT node;
  REF_RECON_RECONSTRUCTION reconstruction = REF_RECON_L2PROJECTION;
  REF_DBL gradation = 10.0;
  REF_INT norm_p = 4;
  REF_DBL aspect_ratio = -1.0;
  REF_INT pos, opt;

  RXS(ref_args_find(argc, argv, "--aspect-ratio", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    aspect_ratio = atof(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf("limit --aspect-ratio to %f\n", aspect_ratio);
  }

  ref_malloc(metric, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  ref_malloc(distance, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  ref_malloc(uplus, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  RSS(ref_phys_wall_distance(ref_grid, ref_dict_bcs, distance), "wall dist");
  ref_mpi_stopwatch_stop(ref_mpi, "wall distance");

  FILE *fp;
  if (mehul_iter==0){
    char filename[20];
    sprintf(filename, "upplus_%d.sol", mehul_iter);
    fp = fopen(filename, "w");
    printf("writing uplus file\n");
  }
  mehul_iter++;

  each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
    RAB(ref_math_divisible(distance[node], spalding_yplus),
        "\nare viscous boundarys set with --viscous-tags or --fun3d-mapbc?"
        "\nwall distance not divisible by y+=1",
        {
          printf("distance %e yplus=1 %e\n", distance[node], spalding_yplus);
        });
    yplus = distance[node] / spalding_yplus;
    RSS(ref_phys_spalding_uplus(yplus, &(uplus[node])), "uplus");

    fprintf(fp, "%f\n", uplus[node]);
  }

  fclose(fp);

  RSS(ref_metric_lp(metric, ref_grid, uplus, reconstruction, norm_p, gradation,
                    aspect_ratio, complexity),
      "lp norm");

  RSS(ref_metric_parse(metric, ref_grid, argc, argv), "parse metric");
  for (opt = 0; opt < argc - 4; opt++) {
    if (strcmp(argv[opt], "--faceid-spacing") == 0) {
      REF_INT faceid;
      REF_DBL set_normal_spacing;
      REF_DBL ceil_normal_spacing;
      REF_DBL tangential_aspect_ratio;

      faceid = atoi(argv[opt + 1]);
      set_normal_spacing = atof(argv[opt + 2]);
      ceil_normal_spacing = atof(argv[opt + 3]);
      tangential_aspect_ratio = atof(argv[opt + 4]);
      if (ref_mpi_once(ref_mpi))
        printf(" --faceid-spacing %d %f %f %f\n", faceid, set_normal_spacing,
               ceil_normal_spacing, tangential_aspect_ratio);
      RSS(ref_metric_faceid_spacing(metric, ref_grid, faceid,
                                    set_normal_spacing, ceil_normal_spacing,
                                    tangential_aspect_ratio),
          "faceid spacing");
    }
  }

  RSS(ref_metric_to_node(metric, ref_grid_node(ref_grid)), "node metric");
  ref_free(uplus);
  ref_free(distance);
  ref_free(metric);
  ref_mpi_stopwatch_stop(ref_mpi, "spalding gradation");
  if (ref_geom_model_loaded(ref_grid_geom(ref_grid)) ||
      ref_geom_meshlinked(ref_grid_geom(ref_grid))) {
    RSS(ref_metric_constrain_curvature(ref_grid), "crv const");
    ref_mpi_stopwatch_stop(ref_mpi, "crv const");
  }

  out_metric = "spalding-output-metric.solb";
  RSS(ref_gather_metric(ref_grid, out_metric), "gather metric");

  return REF_SUCCESS;
}

static REF_STATUS distance_metric_fill(REF_GRID ref_grid, REF_DICT ref_dict_bcs,
                                       int argc, char *argv[]) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_DBL *distance;
  REF_INT node;
  REF_INT pos;
  REF_DBL h0 = 0.0, h1 = 0.0, h2 = 0.0, s1 = 0.0, s2 = 0.0, width = 0.0;
  REF_DBL aspect_ratio = 1.0;
  REF_BOOL have_stepexp = REF_FALSE;
  REF_BOOL have_spacing_table = REF_FALSE;
  REF_DBL *grad_dist;
  REF_RECON_RECONSTRUCTION recon = REF_RECON_L2PROJECTION;
  REF_INT n_tab = 0, max_tab;
  REF_DBL *tab_dist = NULL, *tab_h = NULL, *tab_ar = NULL;

  RXS(ref_args_find(argc, argv, "--aspect-ratio", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    aspect_ratio = atof(argv[pos + 1]);
    aspect_ratio = MAX(1.0, aspect_ratio);
    if (ref_mpi_once(ref_mpi))
      printf("limit --aspect-ratio to %f for --stepexp\n", aspect_ratio);
  }

  RXS(ref_args_find(argc, argv, "--stepexp", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    have_stepexp = REF_TRUE;
    RAS(pos + 6 < argc, "not enough --stepexp args");
    h0 = atof(argv[pos + 1]);
    h1 = atof(argv[pos + 2]);
    h2 = atof(argv[pos + 3]);
    s1 = atof(argv[pos + 4]);
    s2 = atof(argv[pos + 5]);
    width = atof(argv[pos + 6]);
    if (ref_mpi_once(ref_mpi))
      printf("h0 %f h1 %f h2 %f s1 %f s2 %f width %f\n", h0, h1, h2, s1, s2,
             width);
    RAS(h0 > 0.0, "positive h0");
    RAS(h1 > 0.0, "positive h1");
    RAS(h2 > 0.0, "positive h2");
    RAS(s1 > 0.0, "positive s1");
    RAS(s2 > 0.0, "positive s2");
    RAS(width > 0.0, "positive width");
  }

  RXS(ref_args_find(argc, argv, "--spacing-table", &pos), REF_NOT_FOUND,
      "metric arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    FILE *file = NULL;
    char line[1024];
    const char *filename = argv[pos + 1];
    const char *token;
    const char space[] = " ";
    REF_INT ncol;
    if (ref_mpi_once(ref_mpi)) {
      file = fopen(filename, "r");
      if (NULL == (void *)file) printf("unable to open %s\n", filename);
      RNS(file, "unable to open file");
      n_tab = 0;
      while (line == fgets(line, 1024, file)) {
        ncol = 0;
        token = strtok(line, space);
        while (token != NULL) {
          ncol++;
          token = strtok(NULL, space);
        }
        if (ncol >= 2) n_tab++;
      }
      printf(" %d breakpoints in %s\n", n_tab, filename);
      ref_malloc_init(tab_dist, n_tab, REF_DBL, 0.0);
      ref_malloc_init(tab_h, n_tab, REF_DBL, 0.0);
      ref_malloc_init(tab_ar, n_tab, REF_DBL, 1.0);
      RAS(0 == fseek(file, 0, SEEK_SET), "rewind");
      max_tab = n_tab;
      n_tab = 0;
      while (line == fgets(line, 1024, file) && n_tab < max_tab) {
        ncol = 0;
        token = strtok(line, space);
        while (token != NULL) {
          if (0 == ncol) tab_dist[n_tab] = atof(token);
          if (1 == ncol) tab_h[n_tab] = atof(token);
          if (2 == ncol) tab_ar[n_tab] = atof(token);
          ncol++;
          token = strtok(NULL, space);
        }
        if (ncol >= 2) {
          printf(" %f %f %f %d\n", tab_dist[n_tab], tab_h[n_tab], tab_ar[n_tab],
                 n_tab);
          n_tab++;
        }
      }
      fclose(file);
      RSS(ref_mpi_bcast(ref_mpi, (void *)&n_tab, 1, REF_INT_TYPE), "n_tab");
      RAS(n_tab > 2, "table requires 2 entries");
      RSS(ref_mpi_bcast(ref_mpi, (void *)tab_dist, n_tab, REF_DBL_TYPE),
          "n_tab");
      RSS(ref_mpi_bcast(ref_mpi, (void *)tab_h, n_tab, REF_DBL_TYPE), "n_tab");
      RSS(ref_mpi_bcast(ref_mpi, (void *)tab_ar, n_tab, REF_DBL_TYPE), "n_tab");
    } else {
      RSS(ref_mpi_bcast(ref_mpi, (void *)&n_tab, 1, REF_INT_TYPE), "n_tab");
      RAS(n_tab > 2, "table requires 2 entries");
      ref_malloc_init(tab_dist, n_tab, REF_DBL, 0.0);
      ref_malloc_init(tab_h, n_tab, REF_DBL, 0.0);
      ref_malloc_init(tab_ar, n_tab, REF_DBL, 1.0);
      RSS(ref_mpi_bcast(ref_mpi, (void *)tab_dist, n_tab, REF_DBL_TYPE),
          "n_tab");
      RSS(ref_mpi_bcast(ref_mpi, (void *)tab_h, n_tab, REF_DBL_TYPE), "n_tab");
      RSS(ref_mpi_bcast(ref_mpi, (void *)tab_ar, n_tab, REF_DBL_TYPE), "n_tab");
    }
    have_spacing_table = REF_TRUE;
  }

  RAS(have_stepexp != have_spacing_table,
      "set one and only one of --stepexp and --spacing-table");

  ref_malloc(distance, ref_node_max(ref_node), REF_DBL);
  RSS(ref_phys_wall_distance(ref_grid, ref_dict_bcs, distance), "wall dist");
  ref_mpi_stopwatch_stop(ref_mpi, "wall distance");

  ref_malloc(grad_dist, 3 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  RSS(ref_recon_gradient(ref_grid, distance, grad_dist, recon), "grad dist");

  if (have_stepexp) {
    if (aspect_ratio > 0.0) {
      each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
        REF_DBL m[6];
        REF_DBL d[12];
        REF_DBL h;
        REF_DBL s = distance[node];
        RSS(ref_metric_step_exp(s, &h, h0, h1, h2, s1, s2, width), "step exp");
        ref_matrix_eig(d, 0) = 1.0 / (h * h);
        ref_matrix_eig(d, 1) = 1.0 / (aspect_ratio * h * aspect_ratio * h);
        ref_matrix_eig(d, 2) = 1.0 / (aspect_ratio * h * aspect_ratio * h);
        ref_matrix_vec(d, 0, 0) = grad_dist[0 + 3 * node];
        ref_matrix_vec(d, 1, 0) = grad_dist[1 + 3 * node];
        ref_matrix_vec(d, 2, 0) = grad_dist[2 + 3 * node];
        if (REF_SUCCESS == ref_math_normalize(&(d[3]))) {
          RSS(ref_math_orthonormal_system(&(d[3]), &(d[6]), &(d[9])),
              "ortho sys");
          RSS(ref_matrix_form_m(d, m), "form m from d");
        } else {
          m[0] = 1.0 / (h * h);
          m[1] = 0.0;
          m[2] = 0.0;
          m[3] = 1.0 / (h * h);
          m[4] = 0.0;
          m[5] = 1.0 / (h * h);
        }
        if (ref_grid_twod(ref_grid)) RSS(ref_matrix_twod_m(m), "enforce 2d");
        RSS(ref_node_metric_set(ref_node, node, m), "set");
      }
    } else {
      each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
        REF_DBL m[6];
        REF_DBL h;
        REF_DBL s = distance[node];
        RSS(ref_metric_step_exp(s, &h, h0, h1, h2, s1, s2, width), "step exp");
        m[0] = 1.0 / (h * h);
        m[1] = 0.0;
        m[2] = 0.0;
        m[3] = 1.0 / (h * h);
        m[4] = 0.0;
        m[5] = 1.0 / (h * h);
        if (ref_grid_twod(ref_grid)) RSS(ref_matrix_twod_m(m), "enforce 2d");
        RSS(ref_node_metric_set(ref_node, node, m), "set");
      }
    }
  }

  if (have_spacing_table) {
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_DBL m[6];
      REF_DBL d[12];
      REF_DBL h;
      REF_DBL dist = distance[node];
      REF_INT i0, i1;
      REF_DBL t0, t1;
      RSS(ref_sort_search_dbl(n_tab, tab_dist, dist, &i0),
          "first index on range");
      i1 = i0 + 1;
      t1 = 0.0;
      if (ref_math_divisible((dist - tab_dist[i0]),
                             (tab_dist[i1] - tab_dist[i0]))) {
        t1 = (dist - tab_dist[i0]) / (tab_dist[i1] - tab_dist[i0]);
      }
      t1 = MIN(MAX(0.0, t1), 1.0);
      t0 = 1.0 - t1;
      h = t0 * tab_h[i0] + t1 * tab_h[i1];
      aspect_ratio = t0 * tab_ar[i0] + t1 * tab_ar[i1];
      ref_matrix_eig(d, 0) = 1.0 / (h * h);
      ref_matrix_eig(d, 1) = 1.0 / (aspect_ratio * h * aspect_ratio * h);
      ref_matrix_eig(d, 2) = 1.0 / (aspect_ratio * h * aspect_ratio * h);
      ref_matrix_vec(d, 0, 0) = grad_dist[0 + 3 * node];
      ref_matrix_vec(d, 1, 0) = grad_dist[1 + 3 * node];
      ref_matrix_vec(d, 2, 0) = grad_dist[2 + 3 * node];
      if (REF_SUCCESS == ref_math_normalize(&(d[3]))) {
        RSS(ref_math_orthonormal_system(&(d[3]), &(d[6]), &(d[9])),
            "ortho sys");
        RSS(ref_matrix_form_m(d, m), "form m from d");
      } else {
        m[0] = 1.0 / (h * h);
        m[1] = 0.0;
        m[2] = 0.0;
        m[3] = 1.0 / (h * h);
        m[4] = 0.0;
        m[5] = 1.0 / (h * h);
      }
      if (ref_grid_twod(ref_grid)) RSS(ref_matrix_twod_m(m), "enforce 2d");
      RSB(ref_node_metric_set(ref_node, node, m), "set", {
        printf("dist %f h %f ar %f t0 %f t1 %f i0 %d i1 %d\n", dist, h,
               aspect_ratio, t0, t1, i0, i1);
        printf("tab_h[i0] %f tab_h[i1] %f tab_h[i0] %f tab_h[i1] %f\n",
               tab_dist[i0], tab_dist[i1], tab_h[i0], tab_h[i1]);
      });
    }

    ref_free(tab_ar);
    ref_free(tab_h);
    ref_free(tab_dist);
  }

  ref_free(grad_dist);
  ref_free(distance);
  return REF_SUCCESS;
}

static REF_STATUS adapt(REF_MPI ref_mpi_orig, int argc, char *argv[]) {
  char *in_mesh = NULL;
  char *in_metric = NULL;
  char *in_egads = NULL;
  REF_GRID ref_grid = NULL;
  REF_MPI ref_mpi = ref_mpi_orig;
  REF_BOOL distance_metric = REF_FALSE;
  REF_BOOL curvature_metric = REF_TRUE;
  REF_BOOL all_done = REF_FALSE;
  REF_BOOL all_done0 = REF_FALSE;
  REF_BOOL all_done1 = REF_FALSE;
  REF_BOOL form_quads = REF_FALSE;
  REF_BOOL form_prism = REF_FALSE;
  REF_BOOL mesh_exported = REF_FALSE;
  REF_INT pass, passes = 30;
  REF_INT opt, pos;
  REF_LONG ntet;
  REF_DICT ref_dict_bcs = NULL;
  REF_DBL spalding_yplus = -1.0;
  REF_DBL complexity = -1.0;

  if (argc < 3) goto shutdown;
  in_mesh = argv[2];

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", in_mesh);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, in_mesh), "part");
    ref_mpi = ref_grid_mpi(ref_grid); /* ref_grid made a deep copy */
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", in_mesh);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_mesh), "import");
    ref_mpi = ref_grid_mpi(ref_grid); /* ref_grid made a deep copy */
    ref_mpi_stopwatch_stop(ref_mpi, "import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  RXS(ref_args_find(argc, argv, "--meshlink", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) printf("meshlink with %s\n", argv[pos + 1]);
    RSS(ref_meshlink_open(ref_grid, argv[pos + 1]), "meshlink init");
    RSS(ref_meshlink_infer_orientation(ref_grid), "meshlink orient");
  } else {
    RXS(ref_args_char(argc, argv, "--egads", "-g", &in_egads), REF_NOT_FOUND,
        "egads arg search");
    if (NULL != in_egads) {
      if (ref_mpi_once(ref_mpi)) printf("load egads from %s\n", in_egads);
      RSS(ref_egads_load(ref_grid_geom(ref_grid), in_egads), "load egads");
      if (ref_mpi_once(ref_mpi) && ref_geom_effective(ref_grid_geom(ref_grid)))
        printf("EBody Effective Body loaded\n");
      ref_mpi_stopwatch_stop(ref_mpi, "load egads");
    } else {
      if (0 < ref_geom_cad_data_size(ref_grid_geom(ref_grid))) {
        if (ref_mpi_once(ref_mpi))
          printf("load egadslite from .meshb byte stream\n");
        RSS(ref_egads_load(ref_grid_geom(ref_grid), NULL), "load egads");
        if (ref_mpi_once(ref_mpi) &&
            ref_geom_effective(ref_grid_geom(ref_grid)))
          printf("EBody Effective Body loaded\n");
        ref_mpi_stopwatch_stop(ref_mpi, "load egads");
      } else {
        if (ref_mpi_once(ref_mpi)) {
          printf("warning: no geometry loaded, assuming planar faces.\n");
        }
        curvature_metric = REF_FALSE;
      }
    }
  }

  if (ref_geom_model_loaded(ref_grid_geom(ref_grid))) {
    RSS(ref_cell_ncell(ref_grid_tet(ref_grid), ref_grid_node(ref_grid), &ntet),
        "global tets");
    if (0 == ntet) ref_grid_surf(ref_grid) = REF_TRUE;
    RSS(ref_egads_mark_jump_degen(ref_grid), "T and UV jumps; UV degen");
  }
  if (ref_geom_model_loaded(ref_grid_geom(ref_grid)) ||
      ref_geom_meshlinked(ref_grid_geom(ref_grid))) {
    RSS(ref_geom_verify_topo(ref_grid), "geom topo");
    RSS(ref_geom_verify_param(ref_grid), "geom param");
    ref_mpi_stopwatch_stop(ref_mpi, "geom assoc");
  }

  RXS(ref_args_find(argc, argv, "--facelift", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) printf("--facelift %s import\n", argv[pos + 1]);
    RSS(ref_facelift_import(ref_grid, argv[pos + 1]), "attach");
    ref_mpi_stopwatch_stop(ref_mpi, "facelift loaded");
  }

  RXS(ref_args_find(argc, argv, "--surrogate", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) printf("--surrogate %s import\n", argv[pos + 1]);
    RSS(ref_facelift_surrogate(ref_grid, argv[pos + 1]), "attach");
    ref_mpi_stopwatch_stop(ref_mpi, "facelift loaded");
    if (ref_mpi_once(ref_mpi)) printf("constrain all\n");
    RSS(ref_geom_constrain_all(ref_grid), "constrain");
    ref_mpi_stopwatch_stop(ref_mpi, "constrain param");
    if (ref_mpi_once(ref_mpi)) printf("verify constrained param\n");
    RSS(ref_geom_verify_param(ref_grid), "constrained params");
    ref_mpi_stopwatch_stop(ref_mpi, "verify param");
  }

  RXS(ref_args_find(argc, argv, "-t", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos)
    RSS(ref_gather_tec_movie_record_button(ref_grid_gather(ref_grid), REF_TRUE),
        "movie on");

  RXS(ref_args_find(argc, argv, "-s", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    passes = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi)) printf("-s %d adaptation passes\n", passes);
  }

  RXS(ref_args_find(argc, argv, "--partitioner", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    REF_INT part_int = atoi(argv[pos + 1]);
    ref_grid_partitioner(ref_grid) = (REF_MIGRATE_PARTIONER)part_int;
    if (ref_mpi_once(ref_mpi))
      printf("--partitioner %d partitioner\n",
             (int)ref_grid_partitioner(ref_grid));
  }

  RXS(ref_args_find(argc, argv, "--ratio-method", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    ref_grid_node(ref_grid)->ratio_method = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf("--ratio-method %d\n", ref_grid_node(ref_grid)->ratio_method);
  }

  RXS(ref_args_find(argc, argv, "--zip-pcurve", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    ref_geom_zip_pcurve(ref_grid_geom(ref_grid)) = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--zip-pcurve pcurve zipping\n");
  }

  RXS(ref_args_find(argc, argv, "--unlock", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    ref_grid_adapt(ref_grid, unlock_tet) = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--unlock tets from geometry\n");
  }

  RXS(ref_args_find(argc, argv, "--quad", &pos), REF_NOT_FOUND, "arg search");
  if (ref_grid_twod(ref_grid) && REF_EMPTY != pos) {
    form_quads = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--quad form quads on boundary\n");
  }

  RXS(ref_args_find(argc, argv, "--prism", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    form_prism = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--prism form prisms on boundary\n");
  }

  RXS(ref_args_find(argc, argv, "--topo", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    ref_grid_adapt(ref_grid, watch_topo) = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--topo checks active\n");
  }

  RXS(ref_args_char(argc, argv, "--metric", "-m", &in_metric), REF_NOT_FOUND,
      "metric arg search");
  if (NULL != in_metric) {
    if (ref_mpi_once(ref_mpi)) printf("part metric %s\n", in_metric);
    RSS(ref_part_metric(ref_grid_node(ref_grid), in_metric), "part metric");
    curvature_metric = REF_FALSE;
    ref_mpi_stopwatch_stop(ref_mpi, "part metric");
  }

  RSS(ref_dict_create(&ref_dict_bcs), "make dict");

  RXS(ref_args_find(argc, argv, "--av", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) {
      printf("parse AV bcs from EGADS attributes\n");
      RSS(ref_phys_av_tag_attributes(ref_dict_bcs, ref_grid_geom(ref_grid)),
          "unable to parse AV bcs from EGADS attribute");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--fun3d-mapbc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *mapbc;
    mapbc = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("reading fun3d bc map %s\n", mapbc);
      RSS(ref_phys_read_mapbc(ref_dict_bcs, mapbc),
          "unable to read fun3d formatted mapbc");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--viscous-tags", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *tags;
    tags = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("parsing viscous tags\n");
      RSS(ref_phys_parse_tags(ref_dict_bcs, tags),
          "unable to parse viscous tags");
      printf(" %d viscous tags parsed\n", ref_dict_n(ref_dict_bcs));
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--spalding", &pos), REF_NOT_FOUND,
      "metric arg search");
  if (REF_EMPTY != pos && pos < argc - 2) {
    if (0 == ref_dict_n(ref_dict_bcs)) {
      if (ref_mpi_once(ref_mpi))
        printf(
            "\nset viscous boundaries via --fun3d-mapbc or --viscous-tags "
            "to use --spalding\n\n");
      goto shutdown;
    }

    spalding_yplus = atof(argv[pos + 1]);
    complexity = atof(argv[pos + 2]);
    if (ref_mpi_once(ref_mpi))
      printf(" --spalding %e %f law of the wall metric\n", spalding_yplus,
             complexity);
    RAS(complexity > 1.0e-20, "complexity must be greater than zero");
    curvature_metric = REF_TRUE;
  }

  RXS(ref_args_find(argc, argv, "--stepexp", &pos), REF_NOT_FOUND,
      "metric arg search");
  if (REF_EMPTY != pos && pos < argc - 6) {
    if (0 == ref_dict_n(ref_dict_bcs)) {
      if (ref_mpi_once(ref_mpi))
        printf(
            "\nset viscous boundaries via --fun3d-mapbc or --viscous-tags "
            "to use --stepexp\n\n");
      goto shutdown;
    }
    if (ref_mpi_once(ref_mpi)) printf(" --stepexp metric\n");
    distance_metric = REF_TRUE;
    curvature_metric = REF_TRUE;
  }

  RXS(ref_args_find(argc, argv, "--spacing-table", &pos), REF_NOT_FOUND,
      "metric arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (0 == ref_dict_n(ref_dict_bcs)) {
      if (ref_mpi_once(ref_mpi))
        printf(
            "\nset viscous boundaries via --fun3d-mapbc or --viscous-tags "
            "to use --spacing-table\n\n");
      goto shutdown;
    }
    if (ref_mpi_once(ref_mpi))
      printf("--spacing-table metric read from %s\n", argv[pos + 1]);
    distance_metric = REF_TRUE;
    curvature_metric = REF_TRUE;
  }

  RXS(ref_args_find(argc, argv, "--implied-complexity", &pos), REF_NOT_FOUND,
      "metric arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    REF_DBL *metric;
    complexity = atof(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf(" --implied-complexity %f implied metric scaled to complexity\n",
             complexity);
    RAS(complexity > 1.0e-20, "complexity must be greater than zero");
    ref_malloc(metric, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    RSS(ref_metric_imply_from(metric, ref_grid), "imply metric");
    ref_mpi_stopwatch_stop(ref_mpi, "imply metric");
    RSS(ref_metric_set_complexity(metric, ref_grid, complexity),
        "scale metric");
    RSS(ref_metric_parse(metric, ref_grid, argc, argv), "parse metric");
    RSS(ref_metric_to_node(metric, ref_grid_node(ref_grid)), "node metric");
    ref_free(metric);
    curvature_metric = REF_FALSE;
  }

  if (curvature_metric) {
    if (distance_metric) {
      RSS(distance_metric_fill(ref_grid, ref_dict_bcs, argc, argv),
          "distance metric fill");
    } else {
      if (spalding_yplus > 0.0) {
        RSS(spalding_metric(ref_grid, ref_dict_bcs, spalding_yplus, complexity,
                            argc, argv),
            "spalding");
      } else {
        RSS(ref_metric_interpolated_curvature(ref_grid), "interp curve");
        ref_mpi_stopwatch_stop(ref_mpi, "curvature metric");
        RXS(ref_args_find(argc, argv, "--facelift-metric", &pos), REF_NOT_FOUND,
            "arg search");
        if (REF_EMPTY != pos && pos < argc - 1) {
          complexity = atof(argv[pos + 1]);
          if (ref_mpi_once(ref_mpi))
            printf("--facelift-metric %f\n", complexity);
          RAS(complexity > 1.0e-20, "complexity must be greater than zero");
          RSS(ref_facelift_multiscale(ref_grid, complexity), "metric");
          ref_mpi_stopwatch_stop(ref_mpi, "facelift metric");
        }
      }
    }
    RXS(ref_args_find(argc, argv, "--uniform", &pos), REF_NOT_FOUND,
        "arg search");
    if (REF_EMPTY != pos) {
      RSS(ref_metric_parse_to_node(ref_grid, argc, argv), "parse uniform");
    }
  } else {
    if (ref_geom_model_loaded(ref_grid_geom(ref_grid)) ||
        ref_geom_meshlinked(ref_grid_geom(ref_grid))) {
      RSS(ref_metric_constrain_curvature(ref_grid), "crv const");
      RSS(ref_validation_cell_volume(ref_grid), "vol");
      ref_mpi_stopwatch_stop(ref_mpi, "crv const");
    }
    RXS(ref_args_find(argc, argv, "--uniform", &pos), REF_NOT_FOUND,
        "arg search");
    if (REF_EMPTY != pos) {
      RSS(ref_metric_parse_to_node(ref_grid, argc, argv), "parse uniform");
    }
    RSS(ref_grid_cache_background(ref_grid), "cache");
    ref_mpi_stopwatch_stop(ref_mpi, "cache background metric");
  }

  RSS(ref_validation_cell_volume(ref_grid), "vol");

  RSS(ref_migrate_to_balance(ref_grid), "balance");
  RSS(ref_grid_pack(ref_grid), "pack");
  ref_mpi_stopwatch_stop(ref_mpi, "pack");

  for (pass = 0; !all_done && pass < passes; pass++) {
    if (ref_mpi_once(ref_mpi))
      printf("\n pass %d of %d with %d ranks\n", pass + 1, passes,
             ref_mpi_n(ref_mpi));
    if (form_quads && pass == passes - 5)
      RSS(ref_layer_align_quad(ref_grid), "quad");
    if (form_prism && pass == passes / 2)
      RSS(ref_layer_align_prism(ref_grid, ref_dict_bcs), "prism");
    all_done1 = all_done0;
    RSS(ref_adapt_pass(ref_grid, &all_done0), "pass");
    all_done = all_done0 && all_done1 && (pass > MIN(5, passes)) && !form_quads;
    if (curvature_metric) {
      if (distance_metric) {
        RSS(distance_metric_fill(ref_grid, ref_dict_bcs, argc, argv),
            "distance metric fill");
      } else {
        if (spalding_yplus > 0.0) {
        printf("spalding is out of loop -by Mehul\n");  
	/*RSS(spalding_metric(ref_grid, ref_dict_bcs, spalding_yplus,
                              complexity, argc, argv),
              "spalding");*/
        } else {
          RSS(ref_metric_interpolated_curvature(ref_grid), "interp curve");
          ref_mpi_stopwatch_stop(ref_mpi, "curvature metric");
          RXS(ref_args_find(argc, argv, "--facelift-metric", &pos),
              REF_NOT_FOUND, "arg search");
          if (REF_EMPTY != pos && pos < argc - 1) {
            complexity = atof(argv[pos + 1]);
            if (ref_mpi_once(ref_mpi))
              printf("--facelift-metric %f\n", complexity);
            RAS(complexity > 1.0e-20, "complexity must be greater than zero");
            RSS(ref_facelift_multiscale(ref_grid, complexity), "metric");
            ref_mpi_stopwatch_stop(ref_mpi, "facelift metric");
          }
        }
      }
      RXS(ref_args_find(argc, argv, "--uniform", &pos), REF_NOT_FOUND,
          "arg search");
      if (REF_EMPTY != pos) {
        RSS(ref_metric_parse_to_node(ref_grid, argc, argv), "parse uniform");
      }
    } else {
      RSS(ref_metric_synchronize(ref_grid), "sync with background");
      ref_mpi_stopwatch_stop(ref_mpi, "metric sync");
    }
    RSS(ref_validation_cell_volume(ref_grid), "vol");
    RSS(ref_adapt_tattle_faces(ref_grid), "tattle");
    ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "tattle faces");
    RSS(ref_migrate_to_balance(ref_grid), "balance");
    RSS(ref_grid_pack(ref_grid), "pack");
    ref_mpi_stopwatch_stop(ref_mpi, "pack");
  }

  RXS(ref_args_find(argc, argv, "--usm3d", &pos), REF_NOT_FOUND, "parse usm3d");
  if (REF_EMPTY != pos) {
    RSS(ref_egads_enforce_y_symmetry(ref_grid), "RSS");
    RSS(ref_validation_cell_volume(ref_grid), "vol");
  }

  RSS(ref_node_implicit_global_from_local(ref_grid_node(ref_grid)),
      "implicit global");
  ref_mpi_stopwatch_stop(ref_mpi, "implicit global");

  RSS(ref_geom_verify_param(ref_grid), "final params");
  ref_mpi_stopwatch_stop(ref_mpi, "verify final params");

  /* export via -x grid.ext and -f final-surf.tec and -q final-vol.plt
     --export-metric-as final-metic.solb */
  for (opt = 0; opt < argc - 1; opt++) {
    if (strcmp(argv[opt], "-x") == 0) {
      size_t end_of_string;
      mesh_exported = REF_TRUE;
      end_of_string = strlen(argv[opt + 1]);
      if (ref_grid_twod(ref_grid) && (end_of_string >= 6) &&
          (strncmp(&((argv[opt + 1])[end_of_string - 6]), ".ugrid", 6) == 0)) {
        REF_GRID extruded_grid;
        if (ref_mpi_once(ref_mpi))
          printf(
              " extrusion automatically added for ugrid output of 2D mesh.\n");
        RSS(ref_grid_extrude_twod(&extruded_grid, ref_grid, 2), "extrude");
        RXS(ref_args_find(argc, argv, "--axi", &pos), REF_NOT_FOUND,
            "arg search");
        if (REF_EMPTY != pos) {
          if (ref_mpi_once(ref_mpi))
            printf(" --axi convert extrusion to wedge.\n");
          RSS(ref_axi_wedge(extruded_grid), "axi wedge");
        }
        if (ref_mpi_para(ref_mpi)) {
          if (ref_mpi_once(ref_mpi))
            printf("gather " REF_GLOB_FMT " nodes to %s\n",
                   ref_node_n_global(ref_grid_node(extruded_grid)),
                   argv[opt + 1]);
          RSS(ref_gather_by_extension(extruded_grid, argv[opt + 1]),
              "gather -x");
        } else {
          if (ref_mpi_once(ref_mpi))
            printf("export " REF_GLOB_FMT " nodes to %s\n",
                   ref_node_n_global(ref_grid_node(extruded_grid)),
                   argv[opt + 1]);
          RSS(ref_export_by_extension(extruded_grid, argv[opt + 1]),
              "export -x");
        }
        RSS(ref_grid_free(extruded_grid), "free extruded_grid");
      } else {
        if (ref_mpi_para(ref_mpi)) {
          if (ref_mpi_once(ref_mpi))
            printf("gather " REF_GLOB_FMT " nodes to %s\n",
                   ref_node_n_global(ref_grid_node(ref_grid)), argv[opt + 1]);
          RSS(ref_gather_by_extension(ref_grid, argv[opt + 1]), "gather -x");
        } else {
          if (ref_mpi_once(ref_mpi))
            printf("export " REF_GLOB_FMT " nodes to %s\n",
                   ref_node_n_global(ref_grid_node(ref_grid)), argv[opt + 1]);
          RSS(ref_export_by_extension(ref_grid, argv[opt + 1]), "export -x");
        }
      }
    }
    if (strcmp(argv[opt], "-f") == 0) {
      if (ref_mpi_once(ref_mpi))
        printf("gather final surface status %s\n", argv[opt + 1]);
      RSS(ref_gather_surf_status_tec(ref_grid, argv[opt + 1]), "gather -f");
    }
    if (strcmp(argv[opt], "-q") == 0) {
      if (ref_mpi_once(ref_mpi))
        printf("gather final volume status %s\n", argv[opt + 1]);
      RSS(ref_gather_volume_status_tec(ref_grid, argv[opt + 1]), "gather -f");
    }
    if (strcmp(argv[opt], "--export-metric-as") == 0) {
      if (ref_mpi_once(ref_mpi))
        printf("gather final metric as %s\n", argv[opt + 1]);
      RSS(ref_gather_metric(ref_grid, argv[opt + 1]),
          "gather --export-metric-as");
    }
  }

  if (!mesh_exported) {
    char filename[1024];
    snprintf(filename, 1024, "%s-adapted.meshb", in_mesh);
    if (ref_mpi_para(ref_mpi)) {
      if (ref_mpi_once(ref_mpi))
        printf("gather " REF_GLOB_FMT " nodes to %s\n",
               ref_node_n_global(ref_grid_node(ref_grid)), filename);
      RSS(ref_gather_by_extension(ref_grid, filename), "gather backup");
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("export " REF_GLOB_FMT " nodes to %s\n",
               ref_node_n_global(ref_grid_node(ref_grid)), filename);
      RSS(ref_export_by_extension(ref_grid, filename), "export backup");
    }
  }

  RSS(ref_dict_free(ref_dict_bcs), "free");
  RSS(ref_grid_free(ref_grid), "free");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) adapt_help(argv[0]);
  return REF_FAILURE;
}

static void report_interections(REF_GRID ref_grid, const char *project) {
  char filename[1024];
  REF_INT self_intersections = REF_EMPTY;
  snprintf(filename, 1024, "%s-intersect.tec", project);
  printf("probing adapted tessellation self-intersections\n");
  printf("these locations will cause a failure of the initial\n");
  printf("  volume generation and should be fixed with geometry\n");
  printf("  repair or set ESP attribute seg_per_rad larger than 2\n");
  printf("  for involved faces.\n");

  ref_dist_collisions(ref_grid, REF_TRUE, filename, &self_intersections);
  printf("%d segment-triangle intersections detected.\n", self_intersections);
  if (self_intersections > 1) printf("  see locations in %s\n", filename);
}

static REF_STATUS fossilize(REF_GRID ref_grid, const char *fossil_filename,
                            const char *project, const char *mesher,
                            const char *mesher_options) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_GRID fossil_grid;
  REF_NODE ref_node, fossil_node;
  REF_CELL ref_cell, fossil_cell;
  REF_INT node, new_node, *f2g;
  REF_INT nodes[REF_CELL_MAX_SIZE_PER], tempnode, cell, new_cell;
  REF_GLOB global;
  char filename[1024];
  REF_INT group, cell_node;

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", fossil_filename);
    RSS(ref_part_by_extension(&fossil_grid, ref_mpi, fossil_filename), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "part");
    ref_grid_partitioner(ref_grid) = REF_MIGRATE_SINGLE;
    RSS(ref_migrate_to_balance(ref_grid), "migrate to single part");
    RSS(ref_grid_pack(ref_grid), "pack");
    ref_mpi_stopwatch_stop(ref_mpi, "pack");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", fossil_filename);
    RSS(ref_import_by_extension(&fossil_grid, ref_mpi, fossil_filename),
        "import");
    ref_mpi_stopwatch_stop(ref_mpi, "import");
  }

  fossil_node = ref_grid_node(fossil_grid);
  ref_node = ref_grid_node(ref_grid);
  ref_malloc_init(f2g, ref_node_max(fossil_node), REF_INT, REF_EMPTY);
  each_ref_node_valid_node(fossil_node, node) {
    if (!ref_cell_node_empty(ref_grid_tri(fossil_grid), node)) {
      RSS(ref_node_next_global(ref_node, &global), "next global");
      RSS(ref_node_add(ref_node, global, &new_node), "new_node");
      f2g[node] = new_node;
      ref_node_xyz(ref_node, 0, new_node) = ref_node_xyz(fossil_node, 0, node);
      ref_node_xyz(ref_node, 1, new_node) = ref_node_xyz(fossil_node, 1, node);
      ref_node_xyz(ref_node, 2, new_node) = ref_node_xyz(fossil_node, 2, node);
    }
  }

  fossil_cell = ref_grid_tri(fossil_grid);
  ref_cell = ref_grid_tri(ref_grid);
  each_ref_cell_valid_cell_with_nodes(fossil_cell, cell, nodes) {
    tempnode = nodes[0];
    nodes[0] = nodes[1];
    nodes[1] = tempnode;

    nodes[0] = f2g[nodes[0]];
    nodes[1] = f2g[nodes[1]];
    nodes[2] = f2g[nodes[2]];
    nodes[3] = REF_EMPTY;

    RSS(ref_cell_add(ref_cell, nodes, &new_cell), "insert tri");
  }

  if (strncmp(mesher, "t", 1) == 0) {
    if (ref_mpi_once(ref_mpi)) {
      printf("fill volume with TetGen\n");
      RSB(ref_geom_tetgen_volume(ref_grid, project, mesher_options),
          "tetgen surface to volume",
          { report_interections(ref_grid, project); });
    }
    ref_mpi_stopwatch_stop(ref_mpi, "tetgen volume");
  } else if (strncmp(mesher, "a", 1) == 0) {
    if (ref_mpi_once(ref_mpi)) {
      printf("fill volume with AFLR3\n");
      RSB(ref_geom_aflr_volume(ref_grid, project, mesher_options),
          "aflr surface to volume",
          { report_interections(ref_grid, project); });
    }
    ref_mpi_stopwatch_stop(ref_mpi, "aflr volume");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("mesher '%s' not implemented\n", mesher);
    return REF_FAILURE;
  }
  ref_grid_surf(ref_grid) = REF_FALSE; /* needed until vol mesher para */
  RSS(ref_validation_boundary_face(ref_grid), "boundary-interior connectivity");
  ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "boundary-volume check");

  RSS(ref_split_edge_geometry(ref_grid), "split geom");
  ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "split geom");
  RSS(ref_node_synchronize_globals(ref_grid_node(ref_grid)), "sync glob");

  ref_cell = ref_grid_tri(ref_grid);
  each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
    if (REF_EMPTY == nodes[3]) RSS(ref_cell_remove(ref_cell, cell), "rm tri");
  }

  each_ref_node_valid_node(fossil_node, node) {
    if (ref_cell_node_empty(ref_grid_tri(fossil_grid), node)) {
      RSS(ref_node_next_global(ref_node, &global), "next global");
      RSS(ref_node_add(ref_node, global, &new_node), "new_node");
      f2g[node] = new_node;
      ref_node_xyz(ref_node, 0, new_node) = ref_node_xyz(fossil_node, 0, node);
      ref_node_xyz(ref_node, 1, new_node) = ref_node_xyz(fossil_node, 1, node);
      ref_node_xyz(ref_node, 2, new_node) = ref_node_xyz(fossil_node, 2, node);
    }
  }

  each_ref_grid_3d_ref_cell(ref_grid, group, ref_cell) {
    fossil_cell = ref_grid_cell(fossil_grid, group);
    each_ref_cell_valid_cell_with_nodes(fossil_cell, cell, nodes) {
      each_ref_cell_cell_node(ref_cell, cell_node) {
        nodes[cell_node] = f2g[nodes[cell_node]];
      }
      RSS(ref_cell_add(ref_cell, nodes, &new_cell), "insert vol cell");
    }
  }

  snprintf(filename, 1024, "%s-vol.plt", project);
  if (ref_mpi_once(ref_mpi))
    printf("gather " REF_GLOB_FMT " nodes to %s\n",
           ref_node_n_global(ref_grid_node(ref_grid)), filename);
  RSS(ref_gather_by_extension(ref_grid, filename), "vol export");
  ref_mpi_stopwatch_stop(ref_mpi, "export volume");

  RSS(ref_validation_boundary_face(ref_grid), "boundary-interior connectivity");
  ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "boundary-volume check");

  snprintf(filename, 1024, "%s-vol.meshb", project);
  if (ref_mpi_once(ref_mpi))
    printf("gather " REF_GLOB_FMT " nodes to %s\n",
           ref_node_n_global(ref_grid_node(ref_grid)), filename);
  RSS(ref_gather_by_extension(ref_grid, filename), "vol export");
  ref_mpi_stopwatch_stop(ref_mpi, "export volume");

  ref_free(f2g);
  return REF_SUCCESS;
}

static REF_STATUS bootstrap(REF_MPI ref_mpi, int argc, char *argv[]) {
  size_t end_of_string;
  char project[1000];
  char filename[1024];
  REF_GRID ref_grid = NULL;
  REF_INT t_pos = REF_EMPTY;
  REF_INT s_pos = REF_EMPTY;
  REF_INT facelift_pos = REF_EMPTY;
  REF_INT pos = REF_EMPTY;
  REF_INT auto_tparams = REF_EGADS_RECOMMENDED_TPARAM;
  const char *mesher = "tetgen";
  const char *mesher_options = NULL;
  REF_INT passes = 15;
  REF_DBL *global_params = NULL;
  REF_BOOL inspect_evaluation = REF_FALSE;

  if (!ref_egads_allows_construction()) {
    if (ref_mpi_once(ref_mpi))
      printf("bootstrap requires EGADS(full) use ref or refmpifull\n");
    goto shutdown;
  }

  if (argc < 3) goto shutdown;
  end_of_string = MIN(1023, strlen(argv[2]));
  if (7 > end_of_string ||
      strncmp(&(argv[2][end_of_string - 6]), ".egads", 6) != 0)
    goto shutdown;
  strncpy(project, argv[2], end_of_string - 6);
  project[end_of_string - 6] = '\0';

  RSS(ref_grid_create(&ref_grid, ref_mpi), "create");

  RXS(ref_args_find(argc, argv, "--zip-pcurve", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    ref_geom_zip_pcurve(ref_grid_geom(ref_grid)) = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--zip-pcurve pcurve zipping\n");
  }

  RXS(ref_args_find(argc, argv, "--partitioner", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    REF_INT part_int = atoi(argv[pos + 1]);
    ref_grid_partitioner(ref_grid) = (REF_MIGRATE_PARTIONER)part_int;
    if (ref_mpi_once(ref_mpi))
      printf("--partitioner %d partitioner\n",
             (int)ref_grid_partitioner(ref_grid));
  }

  if (ref_mpi_once(ref_mpi)) {
    printf("loading %s.egads\n", project);
  }
  RSS(ref_egads_load(ref_grid_geom(ref_grid), argv[2]), "ld egads");
  if (ref_mpi_once(ref_mpi) && ref_geom_effective(ref_grid_geom(ref_grid)))
    printf("EBody Effective Body loaded\n");
  ref_mpi_stopwatch_stop(ref_mpi, "egads load");

  if (ref_mpi_once(ref_mpi)) {
    REF_BOOL axi = REF_FALSE;
    RXS(ref_args_find(argc, argv, "--axi", &pos), REF_NOT_FOUND, "arg search");
    if (REF_EMPTY != pos) {
      if (ref_mpi_once(ref_mpi)) printf("--axi sets 6022 bc\n");
      axi = REF_TRUE;
    }
    snprintf(filename, 1024, "%s-vol.mapbc", project);
    printf("extracting %s from 'bc_name' attributes\n", filename);
    if (REF_SUCCESS ==
        ref_egads_extract_fun3d_mapbc(ref_grid_geom(ref_grid), filename, axi)) {
      printf("%s extracted\n", filename);
      RXS(ref_args_find(argc, argv, "--usm3d", &pos), REF_NOT_FOUND,
          "arg search");
      if (REF_EMPTY != pos) {
        snprintf(filename, 1024, "%s-usm3d.mapbc", project);
        printf("extracting %s from 'bc_name' attributes\n", filename);
        RSS(ref_egads_extract_usm3d_mapbc(ref_grid_geom(ref_grid), filename),
            "");
        printf("%s extracted\n", filename);
      }
    } else {
      printf("one or more 'bc_name' attributes not set, mapbc not written\n");
      printf(
          " All faces (or edges for 2D) should have bc_name attributes "
          "like so:\n");
      printf("         select face # all faces\n");
      printf("         attribute bc_name $4000_wall\n");
      printf("         select face 5\n");
      printf("         attribute bc_name $5000_farfield\n");
    }
  }

  RXS(ref_args_find(argc, argv, "--auto-tparams", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    auto_tparams = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf("--auto-tparams %d requested\n", auto_tparams);
    if (auto_tparams < 0) {
      auto_tparams = REF_EGADS_ALL_TPARAM;
      if (ref_mpi_once(ref_mpi))
        printf("--auto-tparams %d set to all\n", auto_tparams);
    }
  }

  RXS(ref_args_find(argc, argv, "--global", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 3) {
    ref_malloc(global_params, 3, REF_DBL);
    global_params[0] = atof(argv[pos + 1]);
    global_params[1] = atof(argv[pos + 2]);
    global_params[2] = atof(argv[pos + 3]);
    if (ref_mpi_once(ref_mpi))
      printf("initial tessellation, global param %f %f %f\n", global_params[0],
             global_params[1], global_params[2]);
  } else {
    if (ref_mpi_once(ref_mpi)) printf("initial tessellation, default param\n");
  }
  RSS(ref_egads_tess(ref_grid, auto_tparams, global_params), "tess egads");
  ref_free(global_params);
  global_params = NULL;
  ref_mpi_stopwatch_stop(ref_mpi, "egads tess");
  snprintf(filename, 1024, "%s-init-surf.tec", project);
  if (ref_mpi_once(ref_mpi))
    RSS(ref_export_tec_surf(ref_grid, filename), "dbg surf");
  ref_mpi_stopwatch_stop(ref_mpi, "export init-surf");
  snprintf(filename, 1024, "%s-init-geom.tec", project);
  if (ref_mpi_once(ref_mpi))
    RSS(ref_geom_tec(ref_grid, filename), "geom export");
  ref_mpi_stopwatch_stop(ref_mpi, "export init-geom");
  if (inspect_evaluation) {
    snprintf(filename, 1024, "%s-init-surf.meshb", project);
    if (ref_mpi_once(ref_mpi))
      RSS(ref_export_by_extension(ref_grid, filename), "dbg meshb");
    ref_mpi_stopwatch_stop(ref_mpi, "export init-surf");
  }
  if (ref_mpi_once(ref_mpi)) printf("verify topo\n");
  RSS(ref_geom_verify_topo(ref_grid), "adapt topo");
  ref_mpi_stopwatch_stop(ref_mpi, "verify topo");
  if (ref_mpi_once(ref_mpi)) printf("verify EGADS param\n");
  RSS(ref_geom_verify_param(ref_grid), "egads params");
  ref_mpi_stopwatch_stop(ref_mpi, "verify param");

  if (ref_mpi_once(ref_mpi)) printf("constrain all\n");
  RSS(ref_geom_constrain_all(ref_grid), "constrain");
  ref_mpi_stopwatch_stop(ref_mpi, "constrain param");
  if (ref_mpi_once(ref_mpi)) printf("verify constrained param\n");
  RSS(ref_geom_verify_param(ref_grid), "constrained params");
  ref_mpi_stopwatch_stop(ref_mpi, "verify param");

  if (inspect_evaluation) {
    snprintf(filename, 1024, "%s-const-geom.tec", project);
    if (ref_mpi_once(ref_mpi))
      RSS(ref_geom_tec(ref_grid, filename), "geom export");
    ref_mpi_stopwatch_stop(ref_mpi, "export init-geom");
  }

  if (ref_geom_manifold(ref_grid_geom(ref_grid))) {
    if (ref_mpi_once(ref_mpi)) printf("verify manifold\n");
    RSS(ref_validation_boundary_manifold(ref_grid), "manifold");
    ref_mpi_stopwatch_stop(ref_mpi, "tess verification");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("manifold not required for wirebody\n");
  }

  RXS(ref_args_find(argc, argv, "-t", &t_pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != t_pos)
    RSS(ref_gather_tec_movie_record_button(ref_grid_gather(ref_grid), REF_TRUE),
        "movie on");

  RXS(ref_args_find(argc, argv, "--mesher", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    mesher = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) printf("--mesher %s requested\n", mesher);
  }

  RXS(ref_args_find(argc, argv, "--mesher-options", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    mesher_options = argv[pos + 1];
    if (ref_mpi_once(ref_mpi))
      printf("--mesher-options %s requested\n", mesher_options);
  }

  RXS(ref_args_find(argc, argv, "-s", &s_pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != s_pos && s_pos < argc - 1) {
    passes = atoi(argv[s_pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf("-s %d surface adaptation passes\n", passes);
  }

  RSS(ref_adapt_surf_to_geom(ref_grid, passes), "ad");

  RSS(ref_geom_report_tri_area_normdev(ref_grid), "tri status");
  if (ref_mpi_once(ref_mpi)) printf("verify topo\n");
  RSS(ref_geom_verify_topo(ref_grid), "adapt topo");
  if (ref_mpi_once(ref_mpi)) printf("verify param\n");
  RSS(ref_geom_verify_param(ref_grid), "adapt params");
  ref_mpi_stopwatch_stop(ref_mpi, "surf verification");

  ref_grid_partitioner(ref_grid) = REF_MIGRATE_SINGLE;
  RSS(ref_migrate_to_balance(ref_grid), "migrate to single part");
  RSS(ref_grid_pack(ref_grid), "pack");
  ref_mpi_stopwatch_stop(ref_mpi, "pack");

  snprintf(filename, 1024, "%s-adapt-surf.meshb", project);
  RSS(ref_gather_by_extension(ref_grid, filename), "gather surf meshb");
  snprintf(filename, 1024, "%s-adapt-geom.tec", project);
  if (ref_mpi_once(ref_mpi))
    RSS(ref_geom_tec(ref_grid, filename), "geom export");
  snprintf(filename, 1024, "%s-adapt-surf.tec", project);
  if (ref_mpi_once(ref_mpi))
    RSS(ref_export_tec_surf(ref_grid, filename), "dbg surf");
  snprintf(filename, 1024, "%s-adapt-prop.tec", project);
  RSS(ref_gather_surf_status_tec(ref_grid, filename), "gather surf status");
  ref_mpi_stopwatch_stop(ref_mpi, "export adapt surf");

  snprintf(filename, 1024, "%s-adapt-triage.tec", project);
  RSS(ref_geom_feedback(ref_grid, filename), "feedback");
  ref_mpi_stopwatch_stop(ref_mpi, "geom feedback");

  RXS(ref_args_find(argc, argv, "--facelift", &facelift_pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != facelift_pos && facelift_pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) {
      printf("--facelift %s requested\n", argv[facelift_pos + 1]);
      RSS(ref_facelift_attach(ref_grid), "attach");
    }
    ref_mpi_stopwatch_stop(ref_mpi, "facelift attached");
    if (ref_mpi_once(ref_mpi)) {
      REF_FACELIFT ref_facelift = ref_geom_facelift(ref_grid_geom(ref_grid));
      RSS(ref_export_by_extension(ref_facelift_grid(ref_facelift),
                                  argv[facelift_pos + 1]),
          "facelift export");
      snprintf(filename, 1024, "%s-facelift-geom.tec", project);
      RSS(ref_facelift_tec(ref_facelift, filename), "facelift viz");
    }
    ref_mpi_stopwatch_stop(ref_mpi, "facelift dumped");
    RSS(ref_geom_constrain_all(ref_grid), "constrain");
    ref_mpi_stopwatch_stop(ref_mpi, "constrain param");
    RSS(ref_geom_verify_param(ref_grid), "facelift params");
    ref_mpi_stopwatch_stop(ref_mpi, "verify param");
    RSS(ref_adapt_surf_to_geom(ref_grid, 3), "ad");
    ref_mpi_stopwatch_stop(ref_mpi, "untangle");
    RSS(ref_grid_pack(ref_grid), "pack");
    ref_mpi_stopwatch_stop(ref_mpi, "pack");
  }

  RXS(ref_args_find(argc, argv, "--surrogate", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    REF_FACELIFT ref_facelift;
    REF_GRID surrogate;
    REF_DBL gap;
    REF_GLOB nnode = 0;
    if (ref_mpi_once(ref_mpi)) {
      printf("--surrogate %s requested\n", argv[pos + 1]);
    }
    REIS(REF_MIGRATE_SINGLE, ref_grid_partitioner(ref_grid),
         "parallel implementation is incomplete");
    RSS(ref_geom_max_gap(ref_grid, &gap), "geom gap");
    if (ref_mpi_once(ref_mpi)) printf("original gap %e\n", gap);
    if (ref_mpi_once(ref_mpi)) {
      RSS(ref_grid_deep_copy(&surrogate, ref_grid), "free grid");
      RSS(ref_geom_enrich3(surrogate), "enrich3");
      nnode = ref_node_n_global(ref_grid_node(surrogate));
      RSS(ref_mpi_bcast(ref_mpi, &nnode, 1, REF_GLOB_TYPE), "bcast nnode");
    } else {
      RSS(ref_grid_create(&surrogate, ref_mpi), "create grid");
      RSS(ref_mpi_bcast(ref_mpi, &nnode, 1, REF_GLOB_TYPE), "bcast nnode");
      RSS(ref_node_initialize_n_global(ref_grid_node(surrogate), nnode),
          "init nnodesg");
    }
    RSS(ref_migrate_replicate_ghost(surrogate), "replicant");
    RSS(ref_facelift_create(&ref_facelift, surrogate, REF_TRUE), "create");
    ref_geom_facelift(ref_grid_geom(ref_grid)) = ref_facelift;
    ref_mpi_stopwatch_stop(ref_mpi, "enrich attach surrogate");
    RSS(ref_geom_constrain_all(ref_grid), "constrain");
    RSS(ref_geom_max_gap(ref_grid, &gap), "geom gap");
    if (ref_mpi_once(ref_mpi)) printf("surrogate gap %e\n", gap);
    if (ref_mpi_once(ref_mpi)) {
      printf("gather %s\n", argv[pos + 1]);
    }
    RSS(ref_gather_by_extension(surrogate, argv[pos + 1]), "gather surrogate");
    ref_mpi_stopwatch_stop(ref_mpi, "gather surrogate");
  }

  RXS(ref_args_find(argc, argv, "--fossil", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    snprintf(filename, 1024, "%s-vol.meshb", project);

    RSS(fossilize(ref_grid, argv[pos + 1], project, mesher, mesher_options),
        "fossilize");
    RSS(ref_grid_free(ref_grid), "free grid");

    return REF_SUCCESS;
  }

  if (ref_geom_manifold(ref_grid_geom(ref_grid))) {
    if (strncmp(mesher, "t", 1) == 0) {
      if (ref_mpi_once(ref_mpi)) {
        printf("fill volume with TetGen\n");
        RSB(ref_geom_tetgen_volume(ref_grid, project, mesher_options),
            "tetgen surface to volume",
            { report_interections(ref_grid, project); });
      }
      ref_mpi_stopwatch_stop(ref_mpi, "tetgen volume");
    } else if (strncmp(mesher, "a", 1) == 0) {
      if (ref_mpi_once(ref_mpi)) {
        printf("fill volume with AFLR3\n");
        RSB(ref_geom_aflr_volume(ref_grid, project, mesher_options),
            "aflr surface to volume",
            { report_interections(ref_grid, project); });
      }
      ref_mpi_stopwatch_stop(ref_mpi, "aflr volume");
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("mesher '%s' not implemented\n", mesher);
      goto shutdown;
    }
    ref_grid_surf(ref_grid) = REF_FALSE; /* needed until vol mesher para */
    RSS(ref_validation_boundary_face(ref_grid),
        "boundary-interior connectivity");
    ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "boundary-volume check");
    RSS(ref_split_edge_geometry(ref_grid), "split geom");
    ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "split geom");
    {
      REF_DBL volume, min_volume, max_volume;
      REF_INT degree, max_degree;
      REF_INT node, cell, nodes[REF_CELL_MAX_SIZE_PER];
      REF_NODE ref_node = ref_grid_node(ref_grid);
      REF_CELL ref_cell = ref_grid_tet(ref_grid);
      max_degree = 0;
      each_ref_node_valid_node(ref_node, node) {
        RSS(ref_adj_degree(ref_cell_adj(ref_cell), node, &degree),
            "cell degree");
        max_degree = MAX(max_degree, degree);
      }
      degree = max_degree;
      RSS(ref_mpi_max(ref_mpi, &degree, &max_degree, REF_INT_TYPE), "mpi max");
      min_volume = REF_DBL_MAX;
      max_volume = REF_DBL_MIN;
      each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
        RSS(ref_node_tet_vol(ref_grid_node(ref_grid), nodes, &volume), "vol");
        min_volume = MIN(min_volume, volume);
        max_volume = MAX(max_volume, volume);
      }
      volume = min_volume;
      RSS(ref_mpi_min(ref_mpi, &volume, &min_volume, REF_DBL_TYPE), "mpi min");
      volume = max_volume;
      RSS(ref_mpi_max(ref_mpi, &volume, &max_volume, REF_DBL_TYPE), "mpi max");
      if (ref_mpi_once(ref_mpi)) {
        printf("tet: max degree %d min volume %e max volume %e\n", max_degree,
               min_volume, max_volume);
      }
    }
  } else {
    REF_BOOL flat;
    RSS(ref_egads_twod_flat_z(ref_grid_geom(ref_grid), &flat), "flatness");
    ref_grid_twod(ref_grid) = flat;
    if (ref_mpi_once(ref_mpi)) {
      if (ref_grid_twod(ref_grid)) {
        printf(" 2D mode inferred from model flatness\n");
      } else {
        printf(" model curved, assume 3D surface\n");
      }
    }
  }
  RSS(ref_node_synchronize_globals(ref_grid_node(ref_grid)), "sync glob");

  snprintf(filename, 1024, "%s-vol.meshb", project);
  if (ref_mpi_once(ref_mpi))
    printf("gather " REF_GLOB_FMT " nodes to %s\n",
           ref_node_n_global(ref_grid_node(ref_grid)), filename);
  RSS(ref_gather_by_extension(ref_grid, filename), "vol export");
  ref_mpi_stopwatch_stop(ref_mpi, "export volume");

  RSS(ref_validation_cell_volume(ref_grid), "vol");

  RSS(ref_grid_free(ref_grid), "free grid");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) bootstrap_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS collar(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *input_filename = NULL;
  char *inflate_arg = NULL;
  char inflate_normal[] = "normal";
  char inflate_flat[] = "flat";
  char inflate_radial[] = "radial";
  char *inflate_method = NULL;
  REF_GRID ref_grid = NULL;
  REF_INT nlayers, layer;
  REF_DBL first_thickness, total_thickness, mach, mach_angle_rad;
  REF_DBL alpha_rad = 0.0;
  REF_DBL thickness, total, xshift;
  REF_DBL rate;
  REF_DICT faceids;
  REF_INT pos, opt;
  REF_DBL origin[3];
  REF_BOOL debug = REF_FALSE;
  REF_BOOL extrude_radially = REF_FALSE;
  REF_BOOL on_rails = REF_FALSE;
  REF_BOOL default_export_filename = REF_TRUE;

  pos = REF_EMPTY;
  RXS(ref_args_find(argc, argv, "--debug", &pos), REF_NOT_FOUND,
      "debug search");
  if (REF_EMPTY != pos) {
    debug = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf(" --debug %d\n", (int)debug);
  }

  if (argc < 8) {
    if (ref_mpi_once(ref_mpi)) {
      printf("not enough required arguments\n");
    }
    goto shutdown;
  }
  inflate_arg = argv[2];
  input_filename = argv[3];
  nlayers = atoi(argv[4]);
  first_thickness = atof(argv[5]);
  total_thickness = atof(argv[6]);
  mach = atof(argv[7]);

  inflate_method = NULL;
  if (strncmp(inflate_arg, "n", 1) == 0) {
    inflate_method = inflate_normal;
  } else if (strncmp(inflate_arg, "f", 1) == 0) {
    inflate_method = inflate_flat;
    extrude_radially = REF_TRUE;
    on_rails = REF_TRUE;
  } else if (strncmp(inflate_arg, "r", 1) == 0) {
    inflate_method = inflate_radial;
    extrude_radially = REF_TRUE;
  }
  if (NULL == inflate_method) {
    if (ref_mpi_once(ref_mpi)) {
      printf("unable to parse inflate method >%s<\n", inflate_arg);
    }
    goto shutdown;
  }

  if (ref_mpi_once(ref_mpi)) {
    printf("inflation method %s\n", inflate_method);
    printf("number of layers %d\n", nlayers);
    printf("first thickness %f\n", first_thickness);
    printf("total thickness %f\n", total_thickness);
    printf("mach %f\n", mach);
  }

  if (nlayers <= 0 || first_thickness <= 0.0 || total_thickness <= 0.0 ||
      mach <= 1.0) {
    if (ref_mpi_once(ref_mpi)) {
      printf(
          "number of layers and thicknesses must be positive and "
          "Mach supersonic\n");
    }
    goto shutdown;
  }
  mach_angle_rad = asin(1 / mach);
  RSS(ref_inflate_rate(nlayers, first_thickness, total_thickness, &rate),
      "compute rate");

  if (ref_mpi_once(ref_mpi)) {
    printf("layer growth rate %f\n", rate);
    printf("mach angle %f rad %f deg\n", mach_angle_rad,
           ref_math_in_degrees(mach_angle_rad));
  }

  RSS(ref_dict_create(&faceids), "create");

  RXS(ref_args_find(argc, argv, "--fun3d-mapbc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    const char *mapbc;
    if (pos >= argc - 1) {
      if (ref_mpi_once(ref_mpi)) {
        printf("--fun3d-mapbc requires a filename\n");
      }
      goto shutdown;
    }
    mapbc = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("reading fun3d bc map %s\n", mapbc);
      RSS(ref_phys_read_mapbc_token(faceids, mapbc, "inflate"),
          "unable to read fun3d formatted mapbc");
    }
  }

  RXS(ref_args_find(argc, argv, "--usm3d-mapbc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    const char *mapbc;
    const char *family_name;
    REF_INT bc_type;
    if (pos >= argc - 3) {
      if (ref_mpi_once(ref_mpi)) {
        printf("--usm3d-mapbc requires a filename, family, and bc type\n");
      }
      goto shutdown;
    }
    mapbc = argv[pos + 1];
    family_name = argv[pos + 2];
    bc_type = atoi(argv[pos + 3]);
    if (ref_mpi_once(ref_mpi)) {
      printf("reading usm3d bc map %s family %s bc %d\n", mapbc, family_name,
             bc_type);
      RSS(ref_inflate_read_usm3d_mapbc(faceids, mapbc, family_name, bc_type),
          "faceids from mapbc");
    }
  }

  RSS(ref_dict_bcast(faceids, ref_mpi), "bcast");
  if (ref_mpi_once(ref_mpi)) {
    printf("inflating %d faces\n", ref_dict_n(faceids));
  }
  if (ref_dict_n(faceids) <= 0) {
    if (ref_mpi_once(ref_mpi)) {
      printf("no faces to inflate, use --fun3d-mapbc or --usm3d-mapbc\n");
    }
    goto shutdown;
  }

  ref_mpi_stopwatch_start(ref_mpi);

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", input_filename);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, input_filename), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "core part");
    RSS(ref_migrate_to_balance(ref_grid), "balance");
    ref_mpi_stopwatch_stop(ref_mpi, "balance core");
    RSS(ref_grid_pack(ref_grid), "pack");
    ref_mpi_stopwatch_stop(ref_mpi, "pack core");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", input_filename);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, input_filename), "import");
    ref_mpi_stopwatch_stop(ref_mpi, "core import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  RXS(ref_args_find(argc, argv, "--rotate", &pos), REF_NOT_FOUND,
      "rotate search");
  if (REF_EMPTY != pos) {
    REF_DBL rotate_deg, rotate_rad;
    REF_NODE ref_node = ref_grid_node(ref_grid);
    REF_INT node;
    REF_DBL x, z;
    if (pos >= argc - 1) THROW("--rotate requires a value");
    rotate_deg = atof(argv[pos + 1]);
    rotate_rad = ref_math_in_radians(rotate_deg);
    if (ref_mpi_once(ref_mpi))
      printf(" --rotate %f deg (%f rad)\n", rotate_deg, rotate_rad);

    each_ref_node_valid_node(ref_node, node) {
      x = ref_node_xyz(ref_node, 0, node);
      z = ref_node_xyz(ref_node, 2, node);
      ref_node_xyz(ref_node, 0, node) =
          x * cos(rotate_rad) - z * sin(rotate_rad);
      ref_node_xyz(ref_node, 2, node) =
          x * sin(rotate_rad) + z * cos(rotate_rad);
    }
  }

  RXS(ref_args_find(argc, argv, "--origin", &pos), REF_NOT_FOUND,
      "origin search");
  if (REF_EMPTY != pos) {
    if (pos >= argc - 3) THROW("--origin requires three values");
    origin[0] = atof(argv[pos + 1]);
    origin[1] = atof(argv[pos + 2]);
    origin[2] = atof(argv[pos + 3]);
    if (ref_mpi_once(ref_mpi))
      printf(" --origin %f %f %f from argument\n", origin[0], origin[1],
             origin[2]);
  } else {
    RSS(ref_inflate_origin(ref_grid, faceids, origin), "orig");
    if (ref_mpi_once(ref_mpi))
      printf(" --origin %f %f %f inferred from z-midpoint\n", origin[0],
             origin[1], origin[2]);
  }

  if (debug) {
    RSS(ref_gather_tec_movie_record_button(ref_grid_gather(ref_grid), REF_TRUE),
        "movie on");
    ref_gather_blocking_frame(ref_grid, "core");
  }

  total = 0.0;
  for (layer = 0; layer < nlayers; layer++) {
    thickness = first_thickness * pow(rate, layer);
    total = total + thickness;
    xshift = thickness / tan(mach_angle_rad);

    if (extrude_radially) {
      RSS(ref_inflate_radially(ref_grid, faceids, origin, thickness,
                               mach_angle_rad, alpha_rad, on_rails, debug),
          "inflate");
    } else {
      RSS(ref_inflate_face(ref_grid, faceids, origin, thickness, xshift),
          "inflate");
    }

    if (ref_mpi_once(ref_mpi))
      printf("layer%5d of%5d thickness %10.3e total %10.3e " REF_GLOB_FMT
             " nodes\n",
             layer + 1, nlayers, thickness, total,
             ref_node_n_global(ref_grid_node(ref_grid)));
  }

  ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "inflate");

  if (ref_mpi_once(ref_mpi)) {
    printf("inflated %d faces\n", ref_dict_n(faceids));
    printf("mach %f mach angle %f rad %f deg\n", mach, mach_angle_rad,
           ref_math_in_degrees(mach_angle_rad));
    printf("first thickness %f\n", first_thickness);
    printf("total thickness %f\n", total_thickness);
    printf("rate %f\n", rate);
    printf("layers %d\n", nlayers);
    printf("inflate method %s\n", inflate_method);
  }

  /* export via -x grid.ext and -f final-surf.tec and -q final-vol.plt */
  for (opt = 0; opt < argc - 1; opt++) {
    if (strcmp(argv[opt], "-x") == 0) {
      default_export_filename = REF_FALSE;
      if (ref_mpi_para(ref_mpi)) {
        if (ref_mpi_once(ref_mpi))
          printf("gather " REF_GLOB_FMT " nodes to %s\n",
                 ref_node_n_global(ref_grid_node(ref_grid)), argv[opt + 1]);
        RSS(ref_gather_by_extension(ref_grid, argv[opt + 1]), "gather -x");
        ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "gather");
      } else {
        if (ref_mpi_once(ref_mpi))
          printf("export " REF_GLOB_FMT " nodes to %s\n",
                 ref_node_n_global(ref_grid_node(ref_grid)), argv[opt + 1]);
        RSS(ref_export_by_extension(ref_grid, argv[opt + 1]), "export -x");
        ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "export");
      }
    }
  }

  if (default_export_filename) {
    if (ref_mpi_once(ref_mpi))
      printf("gather " REF_GLOB_FMT " nodes to %s\n",
             ref_node_n_global(ref_grid_node(ref_grid)), "inflated.b8.ugrid");
    RSS(ref_gather_by_extension(ref_grid, "inflated.b8.ugrid"), "gather");
    ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "gather");
  }

  RSS(ref_dict_free(faceids), "free");
  RSS(ref_grid_free(ref_grid), "grid");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) collar_help(argv[0]);
  RSS(ref_grid_free(ref_grid), "grid");
  return REF_FAILURE;
}

static REF_STATUS distance(REF_MPI ref_mpi, int argc, char *argv[]) {
  REF_GRID ref_grid;
  REF_DICT ref_dict_bcs;
  REF_DBL *distance;
  char *in_mesh = NULL;
  char *out_file = NULL;
  REF_INT pos;
  if (argc < 4) goto shutdown;
  in_mesh = argv[2];
  out_file = argv[3];

  RSS(ref_dict_create(&ref_dict_bcs), "create");

  RXS(ref_args_find(argc, argv, "--fun3d-mapbc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *mapbc;
    mapbc = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("reading fun3d bc map %s\n", mapbc);
      RSS(ref_phys_read_mapbc(ref_dict_bcs, mapbc),
          "unable to read fun3d formatted mapbc");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  /* delete this block when f3d uses --fun3d-mapbc */
  RXS(ref_args_find(argc, argv, "--fun3d", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *mapbc;
    mapbc = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      distance_help(argv[0]);
      printf(" use --fun3d-mapbc, --fun3d no longer supported \n");
      printf("reading fun3d bc map %s\n", mapbc);
      RSS(ref_phys_read_mapbc(ref_dict_bcs, mapbc),
          "unable to read fun3d formatted mapbc");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--viscous-tags", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *tags;
    tags = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("parsing viscous tags\n");
      RSS(ref_phys_parse_tags(ref_dict_bcs, tags),
          "unable to parse viscous tags");
      printf(" %d viscous tags parsed\n", ref_dict_n(ref_dict_bcs));
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", in_mesh);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, in_mesh), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", in_mesh);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_mesh), "import");
    ref_mpi_stopwatch_stop(ref_mpi, "import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  RXS(ref_args_find(argc, argv, "--av", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) {
      printf("parse AV bcs from EGADS attributes\n");
      RSS(ref_phys_av_tag_attributes(ref_dict_bcs, ref_grid_geom(ref_grid)),
          "unable to parse AV bcs from EGADS attribute");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  if (0 == ref_dict_n(ref_dict_bcs)) {
    if (ref_mpi_once(ref_mpi))
      printf(
          "\nno solid walls specified\n"
          "set viscous boundaries via --fun3d-mapbc or --viscous-tags\n\n");
    goto shutdown;
  }

  ref_malloc_init(distance, ref_node_max(ref_grid_node(ref_grid)), REF_DBL,
                  -1.0);
  RXS(ref_args_find(argc, argv, "--static", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY == pos) {
    RSS(ref_phys_wall_distance(ref_grid, ref_dict_bcs, distance), "store");
    ref_mpi_stopwatch_stop(ref_mpi, "wall distance");
  } else {
    RSS(ref_phys_wall_distance_static(ref_grid, ref_dict_bcs, distance),
        "store");
    ref_mpi_stopwatch_stop(ref_mpi, "wall distance not balanced");
  }
  if (ref_mpi_once(ref_mpi)) printf("gather %s\n", out_file);
  RSS(ref_gather_scalar_by_extension(ref_grid, 1, distance, NULL, out_file),
      "gather");
  ref_mpi_stopwatch_stop(ref_mpi, "gather");

  ref_free(distance);
  ref_dict_free(ref_dict_bcs);
  ref_grid_free(ref_grid);

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) distance_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS examine(REF_MPI ref_mpi, int argc, char *argv[]) {
  if (argc < 3) goto shutdown;

  RSS(ref_import_examine_header(argv[2]), "examine header");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) examine_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS grow(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *out_file;
  char *in_file;
  char project[1000];
  size_t end_of_string;
  REF_GRID ref_grid = NULL;
  const char *mesher = "tetgen";
  const char *mesher_options = NULL;
  REF_INT pos;

  if (ref_mpi_para(ref_mpi)) {
    RSS(REF_IMPLEMENT, "ref grow is not parallel");
  }
  if (argc < 4) goto shutdown;
  in_file = argv[2];
  out_file = argv[3];
  end_of_string = MIN(1023, strlen(argv[2]));
  if (7 > end_of_string ||
      strncmp(&(argv[2][end_of_string - 6]), ".meshb", 6) != 0)
    goto shutdown;
  strncpy(project, argv[2], end_of_string - 6);
  project[end_of_string - 6] = '\0';

  printf("import %s\n", in_file);
  RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_file), "load surface");

  RXS(ref_args_find(argc, argv, "--mesher", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    mesher = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) printf("--mesher %s requested\n", mesher);
  }

  RXS(ref_args_find(argc, argv, "--mesher-options", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    mesher_options = argv[pos + 1];
    if (ref_mpi_once(ref_mpi))
      printf("--mesher-options %s requested\n", mesher_options);
  }

  if (strncmp(mesher, "t", 1) == 0) {
    if (ref_mpi_once(ref_mpi)) {
      printf("fill volume with TetGen\n");
      RSB(ref_geom_tetgen_volume(ref_grid, project, mesher_options),
          "tetgen surface to volume",
          { report_interections(ref_grid, project); });
    }
    ref_mpi_stopwatch_stop(ref_mpi, "tetgen volume");
  } else if (strncmp(mesher, "a", 1) == 0) {
    if (ref_mpi_once(ref_mpi)) {
      printf("fill volume with AFLR3\n");
      RSB(ref_geom_aflr_volume(ref_grid, project, mesher_options),
          "aflr surface to volume",
          { report_interections(ref_grid, project); });
    }
    ref_mpi_stopwatch_stop(ref_mpi, "aflr volume");
  } else {
    printf("mesher '%s' not implemented\n", mesher);
    goto shutdown;
  }

  ref_grid_surf(ref_grid) = REF_FALSE; /* needed until vol mesher para */
  RSS(ref_validation_boundary_face(ref_grid), "boundary-interior connectivity");
  ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "boundary-volume check");

  RSS(ref_split_edge_geometry(ref_grid), "split geom");
  ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "split geom");

  RSS(ref_node_synchronize_globals(ref_grid_node(ref_grid)), "sync glob");

  printf("export %s\n", out_file);
  RSS(ref_export_by_extension(ref_grid, out_file), "vol export");

  RSS(ref_validation_cell_volume(ref_grid), "vol");

  RSS(ref_grid_free(ref_grid), "create");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) grow_help(argv[0]);
  return REF_FAILURE;
}

REF_FCN static REF_STATUS ref_grid_extrude_field(REF_GRID twod_grid,
                                                 REF_INT ldim,
                                                 REF_DBL *twod_field,
                                                 REF_GRID extruded_grid,
                                                 REF_DBL *extruded_field) {
  REF_NODE ref_node;
  REF_SEARCH ref_search;
  REF_LIST touching;
  REF_INT node, candidate, best, item, i;
  REF_DBL dist, radius, best_dist, position[3];
  REF_BOOL verbose = REF_FALSE;
  RSS(ref_list_create(&touching), "touching list");
  ref_node = ref_grid_node(twod_grid);
  RSS(ref_search_create(&ref_search, ref_node_n(ref_node)), "create search");
  each_ref_node_valid_node(ref_node, node) {
    radius = 0.0;
    RSS(ref_search_insert(ref_search, node, ref_node_xyz_ptr(ref_node, node),
                          radius),
        "ins");
  }
  ref_node = ref_grid_node(extruded_grid);
  each_ref_node_valid_node(ref_node, node) {
    position[0] = ref_node_xyz(ref_grid_node(extruded_grid), 0, node);
    position[1] = ref_node_xyz(ref_grid_node(extruded_grid), 2, node);
    position[2] = 0.0;
    radius = 100.0 * 1.0e-8 *
                 sqrt(position[0] * position[0] + position[1] * position[1] +
                      position[2] + position[2]) +
             MAX(0, position[1] * (1.0 - cos(ref_math_in_radians(1.0))));
    RSS(ref_search_touching(ref_search, touching, position, radius),
        "search tree");
    best_dist = 1.0e+200;
    best = REF_EMPTY;
    each_ref_list_item(touching, item) {
      candidate = ref_list_value(touching, item);
      dist = sqrt(pow(position[0] -
                          ref_node_xyz(ref_grid_node(twod_grid), 0, candidate),
                      2) +
                  pow(position[1] -
                          ref_node_xyz(ref_grid_node(twod_grid), 1, candidate),
                      2) +
                  pow(position[2] -
                          ref_node_xyz(ref_grid_node(twod_grid), 2, candidate),
                      2));
      if (dist < best_dist) {
        best_dist = dist;
        best = candidate;
      }
    }
    if (verbose)
      printf("dist %e position %f %f %f\n", best_dist, position[0], position[1],
             position[2]);
    if (REF_EMPTY != best) {
      for (i = 0; i < ldim; i++) {
        extruded_field[i + ldim * node] = twod_field[i + ldim * best];
      }
    }
    RSS(ref_list_erase(touching), "erase");
  }
  ref_search_free(ref_search);
  ref_list_free(touching);
  return REF_SUCCESS;
}

static REF_STATUS interpolate(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *receipt_solb;
  char *receipt_meshb;
  char *donor_solb;
  char *donor_meshb;
  char *persist_solb;
  REF_GRID donor_grid = NULL;
  REF_GRID receipt_grid = NULL;
  REF_INT ldim, persist_ldim;
  REF_DBL *donor_solution, *receipt_solution;
  REF_INTERP ref_interp;
  REF_INT pos;
  REF_INT faceid;

  if (argc < 6) goto shutdown;
  donor_meshb = argv[2];
  donor_solb = argv[3];
  receipt_meshb = argv[4];
  receipt_solb = argv[5];

  ref_mpi_stopwatch_start(ref_mpi);

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", donor_meshb);
    RSS(ref_part_by_extension(&donor_grid, ref_mpi, donor_meshb), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "donor part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", donor_meshb);
    RSS(ref_import_by_extension(&donor_grid, ref_mpi, donor_meshb), "import");
    ref_mpi_stopwatch_stop(ref_mpi, "donor import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(donor_grid)));

  if (ref_mpi_once(ref_mpi)) printf("part solution %s\n", donor_solb);
  RSS(ref_part_scalar(donor_grid, &ldim, &donor_solution, donor_solb),
      "part solution");
  ref_mpi_stopwatch_stop(ref_mpi, "donor part solution");

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", receipt_meshb);
    RSS(ref_part_by_extension(&receipt_grid, ref_mpi, receipt_meshb), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "receptor part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", receipt_meshb);
    RSS(ref_import_by_extension(&receipt_grid, ref_mpi, receipt_meshb),
        "import");
    ref_mpi_stopwatch_stop(ref_mpi, "receptor import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(receipt_grid)));

  if (ref_mpi_once(ref_mpi)) {
    printf("%d leading dim from " REF_GLOB_FMT " donor nodes to " REF_GLOB_FMT
           " receptor nodes\n",
           ldim, ref_node_n_global(ref_grid_node(donor_grid)),
           ref_node_n_global(ref_grid_node(receipt_grid)));
  }

  RXS(ref_args_find(argc, argv, "--face", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 2) {
    faceid = atoi(argv[pos + 1]);
    persist_solb = argv[pos + 2];
    if (ref_mpi_once(ref_mpi))
      printf("part persist solution %s\n", persist_solb);
    RSS(ref_part_scalar(receipt_grid, &persist_ldim, &receipt_solution,
                        persist_solb),
        "part solution");
    ref_mpi_stopwatch_stop(ref_mpi, "persist part solution");
    REIS(ldim, persist_ldim, "persist leading dimension different than donor");

    if (ref_mpi_once(ref_mpi)) printf("update solution on faceid %d\n", faceid);
    RSS(ref_interp_create(&ref_interp, donor_grid, receipt_grid),
        "make interp");
    RSS(ref_interp_face_only(ref_interp, faceid, ldim, donor_solution,
                             receipt_solution),
        "map");
    ref_mpi_stopwatch_stop(ref_mpi, "update");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("locate receptor nodes\n");
    RSS(ref_interp_create(&ref_interp, donor_grid, receipt_grid),
        "make interp");
    RSS(ref_interp_locate(ref_interp), "map");
    ref_mpi_stopwatch_stop(ref_mpi, "locate");
    if (ref_mpi_once(ref_mpi)) printf("interpolate receptor nodes\n");
    ref_malloc(receipt_solution,
               ldim * ref_node_max(ref_grid_node(receipt_grid)), REF_DBL);
    RSS(ref_interp_scalar(ref_interp, ldim, donor_solution, receipt_solution),
        "interp scalar");
    ref_mpi_stopwatch_stop(ref_mpi, "interp");
  }

  RXS(ref_args_find(argc, argv, "--extrude", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_GRID extruded_grid;
    REF_DBL *extruded_solution = NULL;
    if (ref_mpi_once(ref_mpi)) printf("extrude receptor solution\n");
    RSS(ref_grid_extrude_twod(&extruded_grid, receipt_grid, 2), "extrude");
    ref_malloc(extruded_solution,
               ldim * ref_node_max(ref_grid_node(extruded_grid)), REF_DBL);
    RSS(ref_grid_extrude_field(receipt_grid, ldim, receipt_solution,
                               extruded_grid, extruded_solution),
        "extrude solution");
    if (ref_mpi_once(ref_mpi))
      printf("writing interpolated extruded solution %s\n", receipt_solb);
    RSS(ref_gather_scalar_by_extension(extruded_grid, ldim, extruded_solution,
                                       NULL, receipt_solb),
        "gather recept");
    ref_free(extruded_solution);
    RSS(ref_grid_free(extruded_grid), "free");
  } else {
    if (ref_mpi_once(ref_mpi))
      printf("writing receptor solution %s\n", receipt_solb);
    RSS(ref_gather_scalar_by_extension(receipt_grid, ldim, receipt_solution,
                                       NULL, receipt_solb),
        "gather recept");
    ref_mpi_stopwatch_stop(ref_mpi, "gather receptor");
  }

  ref_free(receipt_solution);
  ref_interp_free(ref_interp);
  RSS(ref_grid_free(receipt_grid), "receipt");
  ref_free(donor_solution);
  RSS(ref_grid_free(donor_grid), "donor");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) interpolate_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS locichem_field_scalar(REF_GRID ref_grid, REF_INT ldim,
                                        REF_DBL *initial_field,
                                        const char *interpolant,
                                        REF_DBL *scalar) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT node;
  REF_BOOL recognized = REF_FALSE;
  REF_BOOL debug = REF_FALSE;

  if (debug)
    RSS(ref_gather_scalar_by_extension(ref_grid, ldim, initial_field, NULL,
                                       "loci-field.plt"),
        "field");

  RSS(ref_validation_finite(ref_grid, ldim, initial_field), "init field");
  if (ref_mpi_once(ref_mpi)) printf("extract %s\n", interpolant);
  if (0 == strcmp(interpolant, "mach")) {
    recognized = REF_TRUE;
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      scalar[node] = initial_field[2 + ldim * node];
    }
  }
  if (0 == strcmp(interpolant, "temperature")) {
    recognized = REF_TRUE;
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      scalar[node] = initial_field[5 + ldim * node];
    }
  }
  if (recognized) ref_mpi_stopwatch_stop(ref_mpi, "extract scalar");

  if (!recognized) {
    REF_INT solb_ldim;
    REF_DBL *solb_scalar;
    if (ref_mpi_once(ref_mpi))
      printf("opening %s as multiscale interpolant\n", interpolant);
    RSS(ref_part_scalar(ref_grid, &solb_ldim, &solb_scalar, interpolant),
        "unable to load interpolant scalar");
    REIS(1, solb_ldim, "expected one interpolant scalar");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      scalar[node] = solb_scalar[node];
    }
    ref_free(solb_scalar);
    ref_mpi_stopwatch_stop(ref_mpi, "read interpolant from file");
  }

  if (debug)
    RSS(ref_gather_scalar_by_extension(ref_grid, 1, scalar, NULL,
                                       "loci-scalar.plt"),
        "scalar");

  return REF_SUCCESS;
}

static REF_STATUS avm_field_scalar(REF_GRID ref_grid, REF_INT ldim,
                                   REF_DBL *initial_field,
                                   const char *interpolant, REF_DBL *scalar) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT node;
  REF_DBL gamma = 1.4;
  REF_BOOL recognized = REF_FALSE;

  RSS(ref_validation_finite(ref_grid, ldim, initial_field), "init field");
  if (ref_mpi_once(ref_mpi)) printf("compute %s\n", interpolant);
  if ((strcmp(interpolant, "mach") == 0) ||
      (strcmp(interpolant, "htot") == 0) ||
      (strcmp(interpolant, "ptot") == 0) ||
      (strcmp(interpolant, "pressure") == 0) ||
      (strcmp(interpolant, "density") == 0) ||
      (strcmp(interpolant, "temperature") == 0)) {
    RAS(5 <= ldim, "expected 5 or more variables per vertex for compressible");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_DBL rho, u, v, w, press, temp, u2, mach2;
      if (ref_grid_twod(ref_grid)) {
        rho = initial_field[0 + ldim * node];
        u = initial_field[1 + ldim * node];
        v = initial_field[2 + ldim * node];
        w = 0.0;
        temp = initial_field[3 + ldim * node];
      } else {
        rho = initial_field[0 + ldim * node];
        u = initial_field[1 + ldim * node];
        v = initial_field[2 + ldim * node];
        w = initial_field[3 + ldim * node];
        temp = initial_field[4 + ldim * node];
      }
      press = rho * temp / gamma;
      u2 = u * u + v * v + w * w;
      RAB(ref_math_divisible(u2, temp), "can not divide by temp", {
        printf("rho = %e  u = %e  v = %e  w = %e  press = %e  temp = %e\n", rho,
               u, v, w, press, temp);
      });
      mach2 = u2 / temp;
      RAB(mach2 >= 0, "negative mach2", {
        printf("rho = %e  u = %e  v = %e  w = %e  press = %e  temp = %e\n", rho,
               u, v, w, press, temp);
      });
      if (strcmp(interpolant, "mach") == 0) {
        recognized = REF_TRUE;
        scalar[node] = sqrt(mach2);
      } else if (strcmp(interpolant, "htot") == 0) {
        recognized = REF_TRUE;
        scalar[node] = temp * (1.0 / (gamma - 1.0)) + 0.5 * u2;
      } else if (strcmp(interpolant, "ptot") == 0) {
        recognized = REF_TRUE;
        scalar[node] =
            press * pow(1.0 + 0.5 * (gamma - 1.0) * mach2, gamma / (gamma - 1));
      } else if (strcmp(interpolant, "pressure") == 0) {
        recognized = REF_TRUE;
        scalar[node] = press;
      } else if (strcmp(interpolant, "density") == 0) {
        recognized = REF_TRUE;
        scalar[node] = rho;
      } else if (strcmp(interpolant, "temperature") == 0) {
        recognized = REF_TRUE;
        scalar[node] = temp;
      }
    }
    if (recognized)
      ref_mpi_stopwatch_stop(ref_mpi, "compute compressible scalar");
  }

  if (!recognized) {
    REF_INT solb_ldim;
    REF_DBL *solb_scalar;
    if (ref_mpi_once(ref_mpi))
      printf("opening %s as multiscale interpolant\n", interpolant);
    RSS(ref_part_scalar(ref_grid, &solb_ldim, &solb_scalar, interpolant),
        "unable to load interpolant scalar");
    REIS(1, solb_ldim, "expected one interpolant scalar");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      scalar[node] = solb_scalar[node];
    }
    ref_free(solb_scalar);
    ref_mpi_stopwatch_stop(ref_mpi, "read interpolant from file");
  }

  return REF_SUCCESS;
}

static REF_STATUS fun3d_field_scalar(REF_GRID ref_grid, REF_INT ldim,
                                     REF_DBL *initial_field,
                                     const char *interpolant, REF_DBL *scalar) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_INT node;
  REF_DBL gamma = 1.4;
  REF_BOOL recognized = REF_FALSE;

  RSS(ref_validation_finite(ref_grid, ldim, initial_field), "init field");
  if (ref_mpi_once(ref_mpi)) printf("compute %s\n", interpolant);
  if (strcmp(interpolant, "incomp") == 0) {
    recognized = REF_TRUE;
    RAS(4 <= ldim,
        "expected 4 or more variables per vertex for incompressible");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_DBL u, v, w, u2;
      u = initial_field[0 + ldim * node];
      v = initial_field[1 + ldim * node];
      w = initial_field[2 + ldim * node];
      /* press = initial_field[3 + ldim * node]; */
      u2 = u * u + v * v + w * w;
      scalar[node] = sqrt(u2);
    }
    ref_mpi_stopwatch_stop(ref_mpi, "compute incompressible scalar");
  }
  if (strcmp(interpolant, "space-time") == 0) {
    recognized = REF_TRUE;
    RAS(4 <= ldim, "expected 4 or more variables per vertex for space-time");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_DBL rho, u, v, press, temp, u2, mach2;
      rho = initial_field[0 + ldim * node];
      u = initial_field[1 + ldim * node];
      v = initial_field[2 + ldim * node];
      press = initial_field[3 + ldim * node];
      RAB(ref_math_divisible(press, rho), "can not divide by rho", {
        printf("rho = %e  u = %e  v = %e  press = %e\n", rho, u, v, press);
      });
      temp = gamma * (press / rho);
      u2 = u * u + v * v;
      RAB(ref_math_divisible(u2, temp), "can not divide by temp", {
        printf("rho = %e  u = %e  v = %e  press = %e  temp = %e\n", rho, u, v,
               press, temp);
      });
      mach2 = u2 / temp;
      RAB(mach2 >= 0, "negative mach2", {
        printf("rho = %e  u = %e  v = %e press = %e  temp = %e\n", rho, u, v,
               press, temp);
      });
      scalar[node] = sqrt(mach2);
    }
    ref_mpi_stopwatch_stop(ref_mpi, "compute incompressible scalar");
  }
  if ((strcmp(interpolant, "mach") == 0) ||
      (strcmp(interpolant, "htot") == 0) ||
      (strcmp(interpolant, "ptot") == 0) ||
      (strcmp(interpolant, "pressure") == 0) ||
      (strcmp(interpolant, "density") == 0) ||
      (strcmp(interpolant, "temperature") == 0)) {
    RAS(5 <= ldim, "expected 5 or more variables per vertex for compressible");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_DBL rho, u, v, w, press, temp, u2, mach2;
      rho = initial_field[0 + ldim * node];
      u = initial_field[1 + ldim * node];
      v = initial_field[2 + ldim * node];
      w = initial_field[3 + ldim * node];
      press = initial_field[4 + ldim * node];
      RAB(ref_math_divisible(press, rho), "can not divide by rho", {
        printf("rho = %e  u = %e  v = %e  w = %e  press = %e\n", rho, u, v, w,
               press);
      });
      temp = gamma * (press / rho);
      u2 = u * u + v * v + w * w;
      RAB(ref_math_divisible(u2, temp), "can not divide by temp", {
        printf("rho = %e  u = %e  v = %e  w = %e  press = %e  temp = %e\n", rho,
               u, v, w, press, temp);
      });
      mach2 = u2 / temp;
      RAB(mach2 >= 0, "negative mach2", {
        printf("rho = %e  u = %e  v = %e  w = %e  press = %e  temp = %e\n", rho,
               u, v, w, press, temp);
      });
      if (strcmp(interpolant, "mach") == 0) {
        recognized = REF_TRUE;
        scalar[node] = sqrt(mach2);
      } else if (strcmp(interpolant, "htot") == 0) {
        recognized = REF_TRUE;
        scalar[node] = temp * (1.0 / (gamma - 1.0)) + 0.5 * u2;
      } else if (strcmp(interpolant, "ptot") == 0) {
        recognized = REF_TRUE;
        scalar[node] =
            press * pow(1.0 + 0.5 * (gamma - 1.0) * mach2, gamma / (gamma - 1));
      } else if (strcmp(interpolant, "pressure") == 0) {
        recognized = REF_TRUE;
        scalar[node] = press;
      } else if (strcmp(interpolant, "density") == 0) {
        recognized = REF_TRUE;
        scalar[node] = rho;
      } else if (strcmp(interpolant, "temperature") == 0) {
        recognized = REF_TRUE;
        scalar[node] = temp;
      }
    }
    if (recognized)
      ref_mpi_stopwatch_stop(ref_mpi, "compute compressible scalar");
  }

  if (!recognized) {
    REF_INT solb_ldim;
    REF_DBL *solb_scalar;
    if (ref_mpi_once(ref_mpi))
      printf("opening %s as solb multiscale interpolant\n", interpolant);
    RSS(ref_part_scalar(ref_grid, &solb_ldim, &solb_scalar, interpolant),
        "unable to load interpolant scalar");
    REIS(1, solb_ldim, "expected one interpolant scalar");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      scalar[node] = solb_scalar[node];
    }
    ref_free(solb_scalar);
    ref_mpi_stopwatch_stop(ref_mpi, "read interpolant from file");
  }

  return REF_SUCCESS;
}

static REF_STATUS fixed_point_metric(
    REF_DBL *metric, REF_GRID ref_grid, REF_INT first_timestep,
    REF_INT last_timestep, REF_INT timestep_increment, const char *in_project,
    const char *solb_middle, REF_RECON_RECONSTRUCTION reconstruction, REF_INT p,
    REF_DBL gradation, REF_DBL complexity, REF_DBL aspect_ratio,
    REF_BOOL strong_sensor_bc, REF_DBL strong_value, REF_DICT ref_dict_bcs) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_DBL *hess, *scalar;
  REF_INT timestep, total_timesteps;
  char solb_filename[1024];
  REF_DBL inv_total;
  REF_INT im, node;
  REF_INT fixed_point_ldim;
  REF_BOOL ensure_finite = REF_TRUE;

  each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
    for (im = 0; im < 6; im++) {
      metric[im + 6 * node] = 0.0; /* initialize */
    }
  }

  ref_malloc(hess, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  total_timesteps = 0;
  for (timestep = first_timestep; timestep <= last_timestep;
       timestep += timestep_increment) {
    snprintf(solb_filename, 1024, "%s%s%d.solb", in_project, solb_middle,
             timestep);
    if (ref_mpi_once(ref_mpi))
      printf("read and hess recon for %s\n", solb_filename);
    RSS(ref_part_scalar(ref_grid, &fixed_point_ldim, &scalar, solb_filename),
        "unable to load scalar");
    REIS(1, fixed_point_ldim, "expected one scalar");
    if (ensure_finite)
      RSS(ref_validation_finite(ref_grid, fixed_point_ldim, scalar),
          "input scalar");
    if (strong_sensor_bc) {
      RSS(ref_phys_strong_sensor_bc(ref_grid, scalar, strong_value,
                                    ref_dict_bcs),
          "apply strong sensor bc");
      if (ensure_finite)
        RSS(ref_validation_finite(ref_grid, fixed_point_ldim, scalar),
            "strong scalar");
    }
    RSS(ref_recon_hessian(ref_grid, scalar, hess, reconstruction), "hess");
    if (ensure_finite)
      RSS(ref_validation_finite(ref_grid, 6, hess), "recon hess");
    ref_free(scalar);
    total_timesteps++;
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      for (im = 0; im < 6; im++) {
        metric[im + 6 * node] += hess[im + 6 * node];
      }
    }
    if (ensure_finite)
      RSS(ref_validation_finite(ref_grid, 6, metric), "metric sum");
  }
  free(hess);
  ref_mpi_stopwatch_stop(ref_mpi, "all timesteps processed");

  RAS(0 < total_timesteps, "expected one or more timesteps");
  inv_total = 1.0 / (REF_DBL)total_timesteps;
  each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
    for (im = 0; im < 6; im++) {
      metric[im + 6 * node] *= inv_total;
    }
  }
  if (ensure_finite)
    RSS(ref_validation_finite(ref_grid, 6, metric), "metric avg");

  RSS(ref_recon_roundoff_limit(metric, ref_grid),
      "floor metric eigenvalues based on grid size and solution jitter");
  RSS(ref_metric_local_scale(metric, ref_grid, p), "local lp norm scaling");
  RSS(ref_metric_limit_aspect_ratio(metric, ref_grid, aspect_ratio),
      "limit aspect ratio");
  ref_mpi_stopwatch_stop(ref_mpi, "limit aspect ratio");
  ref_mpi_stopwatch_stop(ref_mpi, "local scale metric");
  RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                         complexity),
      "gradation at complexity");
  ref_mpi_stopwatch_stop(ref_mpi, "metric gradation and complexity");

  return REF_SUCCESS;
}

static REF_STATUS ddes_fixed_point_metric(
    REF_DBL *metric, REF_GRID ref_grid, REF_INT first_timestep,
    REF_INT last_timestep, REF_INT timestep_increment, const char *in_project,
    const char *solb_middle, REF_RECON_RECONSTRUCTION reconstruction, REF_INT p,
    REF_DBL gradation, REF_DBL complexity, REF_DICT ref_dict_bcs, REF_INT ldim,
    REF_DBL *field, REF_DBL mach, REF_DBL reynolds_number,
    REF_DBL aspect_ratio) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_DBL *hess, *scalar;
  REF_INT timestep, total_timesteps;
  char solb_filename[1024];
  REF_DBL inv_total;
  REF_INT im, node;
  REF_INT fixed_point_ldim;
  REF_DBL *distance, *blend, *aspect_ratio_field;
  REF_DBL *u, *gradu, *gradv, *gradw;

  if (ref_mpi_once(ref_mpi))
    printf("--ddes %f Mach %e Reynolds number of %d ldim\n", mach,
           reynolds_number, ldim);

  RAS(ref_dict_n(ref_dict_bcs) > 0, "no viscous walls set");

  ref_malloc(blend, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  ref_malloc(distance, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  RSS(ref_phys_wall_distance(ref_grid, ref_dict_bcs, distance), "wall dist");
  ref_mpi_stopwatch_stop(ref_mpi, "wall distance");

  ref_malloc_init(u, ref_node_max(ref_node), REF_DBL, 0.0);
  ref_malloc_init(gradu, 3 * ref_node_max(ref_node), REF_DBL, 0.0);
  ref_malloc_init(gradv, 3 * ref_node_max(ref_node), REF_DBL, 0.0);
  ref_malloc_init(gradw, 3 * ref_node_max(ref_node), REF_DBL, 0.0);

  each_ref_node_valid_node(ref_node, node) { u[node] = field[1 + ldim * node]; }
  RSS(ref_recon_gradient(ref_grid, u, gradu, reconstruction), "gu");
  ref_mpi_stopwatch_stop(ref_mpi, "gradu");
  each_ref_node_valid_node(ref_node, node) { u[node] = field[2 + ldim * node]; }
  RSS(ref_recon_gradient(ref_grid, u, gradv, reconstruction), "gv");
  ref_mpi_stopwatch_stop(ref_mpi, "gradv");
  each_ref_node_valid_node(ref_node, node) { u[node] = field[3 + ldim * node]; }
  RSS(ref_recon_gradient(ref_grid, u, gradw, reconstruction), "gw");
  ref_mpi_stopwatch_stop(ref_mpi, "gradw");

  each_ref_node_valid_node(ref_node, node) {
    REF_DBL sqrtgrad;
    REF_DBL nu, fd;
    sqrtgrad = sqrt(gradu[0 + 3 * node] * gradu[0 + 3 * node] +
                    gradu[1 + 3 * node] * gradu[1 + 3 * node] +
                    gradu[2 + 3 * node] * gradu[2 + 3 * node] +
                    gradv[0 + 3 * node] * gradv[0 + 3 * node] +
                    gradv[1 + 3 * node] * gradv[1 + 3 * node] +
                    gradv[2 + 3 * node] * gradv[2 + 3 * node] +
                    gradw[0 + 3 * node] * gradw[0 + 3 * node] +
                    gradw[1 + 3 * node] * gradw[1 + 3 * node] +
                    gradw[2 + 3 * node] * gradw[2 + 3 * node]);
    nu = field[5 + ldim * node];
    RSS(ref_phys_ddes_blend(mach, reynolds_number, sqrtgrad, distance[node], nu,
                            &fd),
        "blend");
    blend[node] = fd;
  }
  ref_free(distance);

  ref_malloc(hess, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  total_timesteps = 0;
  for (timestep = first_timestep; timestep <= last_timestep;
       timestep += timestep_increment) {
    snprintf(solb_filename, 1024, "%s%s%d.solb", in_project, solb_middle,
             timestep);
    if (ref_mpi_once(ref_mpi))
      printf("read and hess recon for %s\n", solb_filename);
    RSS(ref_part_scalar(ref_grid, &fixed_point_ldim, &scalar, solb_filename),
        "unable to load scalar");
    REIS(1, fixed_point_ldim, "expected one scalar");
    RSS(ref_recon_hessian(ref_grid, scalar, hess, reconstruction), "hess");
    ref_free(scalar);
    total_timesteps++;
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      for (im = 0; im < 6; im++) {
        metric[im + 6 * node] += hess[im + 6 * node];
      }
    }
  }
  free(hess);
  ref_mpi_stopwatch_stop(ref_mpi, "all timesteps processed");

  RAS(0 < total_timesteps, "expected one or more timesteps");
  inv_total = 1.0 / (REF_DBL)total_timesteps;
  each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
    for (im = 0; im < 6; im++) {
      metric[im + 6 * node] *= inv_total;
    }
  }
  RSS(ref_recon_roundoff_limit(metric, ref_grid),
      "floor metric eigenvalues based on grid size and solution jitter");
  RSS(ref_metric_local_scale(metric, ref_grid, p), "local lp norm scaling");
  ref_mpi_stopwatch_stop(ref_mpi, "local scale metric");

  ref_malloc(aspect_ratio_field, ref_node_max(ref_grid_node(ref_grid)),
             REF_DBL);
  each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
    REF_DBL blend_clip;
    REF_DBL thresh = 0.5;
    REF_DBL aspect_ratio_target = 1.0;
    /* blend: 0-RANS 1-LES */
    /* when blend is less than thresh, keep RANS */
    if (aspect_ratio > 0.999) aspect_ratio_target = aspect_ratio;
    blend_clip = MAX(0.0, (blend[node] - thresh) / (1.0 - thresh));
    if (ref_math_divisible(aspect_ratio_target, blend_clip)) {
      aspect_ratio_field[node] = aspect_ratio_target / blend_clip;
    } else {
      aspect_ratio_field[node] = 1.0e15; /* unlimited */
    }
  }
  RSS(ref_metric_limit_aspect_ratio_field(metric, ref_grid, aspect_ratio_field),
      "limit aspect ratio");
  ref_free(aspect_ratio_field);
  ref_free(blend);

  RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                         complexity),
      "gradation at complexity");
  ref_mpi_stopwatch_stop(ref_mpi, "metric gradation and complexity");

  return REF_SUCCESS;
}

static REF_STATUS extract_displaced_xyz(REF_NODE ref_node, REF_INT *ldim,
                                        REF_DBL **initial_field,
                                        REF_DBL **displaced) {
  REF_INT i, node;

  ref_malloc(*displaced, 3 * ref_node_max(ref_node), REF_DBL);
  each_ref_node_valid_node(ref_node, node) {
    for (i = 0; i < 3; i++) {
      (*displaced)[i + 3 * node] = (*initial_field)[i + (*ldim) * node];
    }
  }
  (*ldim) -= 3;
  each_ref_node_valid_node(ref_node, node) {
    for (i = 0; i < (*ldim); i++) {
      (*initial_field)[i + (*ldim) * node] =
          (*initial_field)[i + 3 + ((*ldim) + 3) * node];
    }
  }
  ref_realloc(*initial_field, (*ldim) * ref_node_max(ref_node), REF_DBL);
  return REF_SUCCESS;
}

static REF_STATUS moving_fixed_point_metric(
    REF_DBL *metric, REF_GRID ref_grid, REF_INT first_timestep,
    REF_INT last_timestep, REF_INT timestep_increment, const char *in_project,
    const char *solb_middle, REF_RECON_RECONSTRUCTION reconstruction, REF_INT p,
    REF_DBL gradation, REF_DBL complexity) {
  REF_MPI ref_mpi = ref_grid_mpi(ref_grid);
  REF_NODE ref_node = ref_grid_node(ref_grid);
  REF_DBL *hess, *scalar;
  REF_DBL *jac, *x, *grad, *this_metric, *xyz, det;
  REF_INT timestep, total_timesteps;
  char solb_filename[1024];
  REF_DBL inv_total;
  REF_INT im, node;
  REF_INT fixed_point_ldim;
  REF_DBL *displaced;
  REF_INT i, j;

  ref_malloc(hess, 6 * ref_node_max(ref_node), REF_DBL);
  ref_malloc(this_metric, 6 * ref_node_max(ref_node), REF_DBL);
  ref_malloc(jac, 9 * ref_node_max(ref_node), REF_DBL);
  ref_malloc(x, ref_node_max(ref_node), REF_DBL);
  ref_malloc(grad, 3 * ref_node_max(ref_node), REF_DBL);
  ref_malloc(xyz, 3 * ref_node_max(ref_node), REF_DBL);

  total_timesteps = 0;
  for (timestep = first_timestep; timestep <= last_timestep;
       timestep += timestep_increment) {
    snprintf(solb_filename, 1024, "%s%s%d.solb", in_project, solb_middle,
             timestep);
    if (ref_mpi_once(ref_mpi))
      printf("read and hess recon for %s\n", solb_filename);
    RSS(ref_part_scalar(ref_grid, &fixed_point_ldim, &scalar, solb_filename),
        "unable to load scalar");
    REIS(4, fixed_point_ldim, "expected x,y,z and one scalar");
    RSS(extract_displaced_xyz(ref_node, &fixed_point_ldim, &scalar, &displaced),
        "disp");
    if (ref_grid_twod(ref_grid)) {
      each_ref_node_valid_node(ref_node, node) {
        displaced[1 + 3 * node] = displaced[2 + 3 * node];
        displaced[2 + 3 * node] = 0.0;
      }
    }
    for (j = 0; j < 3; j++) {
      each_ref_node_valid_node(ref_node, node) {
        x[node] = displaced[j + 3 * node];
      }
      RSS(ref_recon_gradient(ref_grid, x, grad, reconstruction), "recon x");
      if (ref_grid_twod(ref_grid)) {
        each_ref_node_valid_node(ref_node, node) { grad[2 + 3 * node] = 1.0; }
      }
      each_ref_node_valid_node(ref_node, node) {
        for (i = 0; i < 3; i++) {
          jac[i + 3 * j + 9 * node] = grad[i + 3 * node];
        }
      }
    }

    each_ref_node_valid_node(ref_node, node) {
      for (i = 0; i < 3; i++) {
        xyz[i + 3 * node] = ref_node_xyz(ref_node, i, node);
        ref_node_xyz(ref_node, i, node) = displaced[i + 3 * node];
      }
    }
    RSS(ref_recon_hessian(ref_grid, scalar, hess, reconstruction), "hess");
    RSS(ref_recon_roundoff_limit(hess, ref_grid),
        "floor metric eigenvalues based on grid size and solution jitter");
    each_ref_node_valid_node(ref_node, node) {
      for (i = 0; i < 3; i++) {
        ref_node_xyz(ref_node, i, node) = xyz[i + 3 * node];
      }
    }

    each_ref_node_valid_node(ref_node, node) {
      RSS(ref_matrix_jac_m_jact(&(jac[9 * node]), &(hess[6 * node]),
                                &(this_metric[6 * node])),
          "J M J^t");

      RSS(ref_matrix_det_gen(3, &(jac[9 * node]), &det), "gen det");
      for (i = 0; i < 6; i++) {
        this_metric[i + 6 * node] *= pow(ABS(det), 1.0 / (REF_DBL)p);
      }
    }

    total_timesteps++;
    each_ref_node_valid_node(ref_node, node) {
      for (im = 0; im < 6; im++) {
        metric[im + 6 * node] += this_metric[im + 6 * node];
      }
    }

    ref_free(displaced);
    ref_free(scalar);
  }
  free(xyz);
  free(grad);
  free(x);
  free(jac);
  free(this_metric);
  free(hess);
  ref_mpi_stopwatch_stop(ref_mpi, "all timesteps processed");

  RAS(0 < total_timesteps, "expected one or more timesteps");
  inv_total = 1.0 / (REF_DBL)total_timesteps;
  each_ref_node_valid_node(ref_node, node) {
    for (im = 0; im < 6; im++) {
      metric[im + 6 * node] *= inv_total;
    }
  }
  RSS(ref_recon_roundoff_limit(metric, ref_grid),
      "floor metric eigenvalues based on grid size and solution jitter");
  RSS(ref_metric_local_scale(metric, ref_grid, p), "local lp norm scaling");
  ref_mpi_stopwatch_stop(ref_mpi, "local scale metric");
  RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                         complexity),
      "gradation at complexity");
  ref_mpi_stopwatch_stop(ref_mpi, "metric gradation and complexity");
  return REF_SUCCESS;
}

static REF_STATUS remove_initial_field_adjoint(REF_NODE ref_node, REF_INT *ldim,
                                               REF_DBL **initial_field) {
  REF_INT i, node;
  RAS((*ldim) % 2 == 0, "volume field should have a even leading dimension");
  (*ldim) /= 2;
  each_ref_node_valid_node(ref_node, node) {
    if (0 != node) {
      for (i = 0; i < (*ldim); i++) {
        (*initial_field)[i + (*ldim) * node] =
            (*initial_field)[i + 2 * (*ldim) * node];
      }
    }
  }
  ref_realloc(*initial_field, (*ldim) * ref_node_max(ref_node), REF_DBL);
  return REF_SUCCESS;
}

static REF_STATUS mask_strong_bc_adjoint(REF_GRID ref_grid,
                                         REF_DICT ref_dict_bcs, REF_INT ldim,
                                         REF_DBL *prim_dual) {
  REF_BOOL *replace;
  ref_malloc(replace, ldim * ref_node_max(ref_grid_node(ref_grid)), REF_BOOL);
  RSS(ref_phys_mask_strong_bcs(ref_grid, ref_dict_bcs, replace, ldim), "mask");
  RSS(ref_recon_extrapolate_kexact(ref_grid, prim_dual, replace, ldim),
      "extrapolate kexact");
  ref_free(replace);

  return REF_SUCCESS;
}

static REF_STATUS parse_p(int argc, char *argv[], REF_INT *p) {
  REF_INT pos;
  *p = 2;
  RXS(ref_args_find(argc, argv, "--opt-goal", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    *p = 1;
  }
  RXS(ref_args_find(argc, argv, "--cons-euler", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    *p = 1;
  }
  RXS(ref_args_find(argc, argv, "--cons-visc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos + 3 < argc) {
    *p = 1;
  }
  RXS(ref_args_find(argc, argv, "--norm-power", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos + 1 < argc) {
    *p = atoi(argv[pos + 1]);
  }
  return REF_SUCCESS;
}

REF_FCN static REF_STATUS ref_subcommand_report_error(
    REF_DBL *metric, REF_GRID ref_grid, REF_DBL *scalar,
    REF_RECON_RECONSTRUCTION reconstruction, REF_DBL complexity) {
  REF_DBL *hess, *error;
  REF_DBL total_error, h, d;
  ref_malloc(hess, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
  ref_malloc(error, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);

  RSS(ref_recon_hessian(ref_grid, scalar, hess, reconstruction), "hess");
  RSS(ref_metric_interpolation_error(metric, hess, ref_grid, error), "error")
  RSS(ref_metric_integrate_error(ref_grid, error, &total_error), "int")
  ref_free(error);
  ref_free(hess);
  d = 3.0;
  if (ref_grid_twod(ref_grid)) d = 2.0;
  h = pow(complexity, -1.0 / d);
  if (ref_mpi_once(ref_grid_mpi(ref_grid)))
    printf("complexity, h=C^(-1/d), and error est. %e %e %e\n", complexity, h,
           total_error);
  return REF_SUCCESS;
}

static REF_STATUS loop(REF_MPI ref_mpi_orig, int argc, char *argv[]) {
  char *in_project = NULL;
  char *out_project = NULL;
  char *in_egads = NULL;
  char filename[1024];
  REF_GRID ref_grid = NULL;
  REF_MPI ref_mpi = ref_mpi_orig;
  REF_GRID extruded_grid = NULL;
  REF_BOOL all_done = REF_FALSE;
  REF_BOOL all_done0 = REF_FALSE;
  REF_BOOL all_done1 = REF_FALSE;
  REF_INT pass, passes = 30;
  REF_INT ldim;
  REF_DBL *initial_field, *ref_field, *extruded_field = NULL, *scalar, *metric;
  REF_DBL *displaced = NULL;
  REF_INT p = 2;
  REF_DBL gradation = -1.0, complexity;
  REF_DBL aspect_ratio = -1.0;
  REF_RECON_RECONSTRUCTION reconstruction = REF_RECON_L2PROJECTION;
  REF_BOOL buffer = REF_FALSE;
  REF_BOOL multiscale_metric;
  REF_DICT ref_dict_bcs = NULL;
  REF_BOOL strong_sensor_bc = REF_FALSE;
  REF_DBL strong_value = 0.0;
  REF_BOOL form_quads = REF_FALSE;
  REF_INT pos;
  REF_INT fixed_point_pos, deforming_pos;
  const char *mach_interpolant = "mach";
  const char *interpolant = mach_interpolant;

  const char *lb8_ugrid = "lb8.ugrid";
  const char *b8_ugrid = "b8.ugrid";
  const char *i_like_grid = "grid";
  const char *avm_grid = "avm";
  const char *mesh_export_extension = lb8_ugrid;

  const char *fun3d_soln = "_volume.solb";
  const char *usm3d_soln = "_volume.plt";
  const char *i_like_soln = ".restart_sol";
  const char *avm_soln = ".rst";
  const char *locichem_soln = ".plt";
  const char *soln_import_extension = fun3d_soln;

  const char *fun3d_restart = "-restart.solb";
  const char *usm3d_restart = ".solb";
  const char *i_like_restart = ".restart_sol";
  const char *avm_restart = "-restart.rst";
  const char *locichem_restart = "-restart.plt";
  const char *soln_export_extension = fun3d_restart;

  if (argc < 5) goto shutdown;
  in_project = argv[2];
  out_project = argv[3];
  complexity = atof(argv[4]);

  RSS(parse_p(argc, argv, &p), "parse p");

  gradation = -1.0;
  RXS(ref_args_find(argc, argv, "--gradation", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (pos >= argc - 1) {
      if (ref_mpi_once(ref_mpi))
        printf("option missing value: --gradation <gradation>\n");
      goto shutdown;
    }
    gradation = atof(argv[pos + 1]);
  }

  if (ref_mpi_once(ref_mpi)) {
    printf("complexity %f\n", complexity);
    printf("Lp=%d\n", p);
    printf("gradation %f\n", gradation);
    printf("reconstruction %d\n", (int)reconstruction);
  }
  RAS(complexity > 1.0e-20, "complexity must be greater than zero");

  aspect_ratio = -1.0;
  RXS(ref_args_find(argc, argv, "--aspect-ratio", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (pos >= argc - 1) {
      if (ref_mpi_once(ref_mpi))
        printf("option missing value: --aspect-ratio <aspect-ratio>\n");
      goto shutdown;
    }
    aspect_ratio = atof(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf(
          "  --aspect-ratio %f detected, not implemented for all metric "
          "options\n",
          aspect_ratio);
  }

  buffer = REF_FALSE;
  RXS(ref_args_find(argc, argv, "--buffer", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    buffer = REF_TRUE;
  }

  RXS(ref_args_find(argc, argv, "--interpolant", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    interpolant = argv[pos + 1];
  }

  RXS(ref_args_find(argc, argv, "--usm3d", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    mesh_export_extension = b8_ugrid;
    soln_import_extension = usm3d_soln;
    soln_export_extension = usm3d_restart;
  }

  RXS(ref_args_find(argc, argv, "--i-like-adaptation", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    mesh_export_extension = i_like_grid;
    soln_import_extension = i_like_soln;
    soln_export_extension = i_like_restart;
  }

  RXS(ref_args_find(argc, argv, "--av", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    mesh_export_extension = avm_grid;
    soln_import_extension = avm_soln;
    soln_export_extension = avm_restart;
  }

  RXS(ref_args_find(argc, argv, "--locichem", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    mesh_export_extension = lb8_ugrid;
    soln_import_extension = locichem_soln;
    soln_export_extension = locichem_restart;
  }

  RXS(ref_args_find(argc, argv, "--mesh-extension", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    mesh_export_extension = argv[pos + 1];
  }

  RXS(ref_args_find(argc, argv, "-s", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    passes = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi)) printf("-s %d adaptation passes\n", passes);
  }

  RSS(ref_dict_create(&ref_dict_bcs), "make dict");

  RXS(ref_args_find(argc, argv, "--fun3d-mapbc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *mapbc;
    mapbc = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("reading fun3d bc map %s\n", mapbc);
      RSS(ref_phys_read_mapbc(ref_dict_bcs, mapbc),
          "unable to read fun3d formatted mapbc");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--viscous-tags", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *tags;
    tags = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("parsing viscous tags\n");
      RSS(ref_phys_parse_tags(ref_dict_bcs, tags),
          "unable to parse viscous tags");
      printf(" %d viscous tags parsed\n", ref_dict_n(ref_dict_bcs));
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--strong-sensor-bc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    RAS(pos + 1 < argc, "--strong-sensor-bc <value>");
    strong_sensor_bc = REF_TRUE;
    strong_value = atof(argv[pos + 1]);
  }

  RXS(ref_args_find(argc, argv, "--i-like-adaptation", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    RAS(!ref_mpi_para(ref_mpi), "--i-like-adaptation is not parallel");
    snprintf(filename, 1024, "%s.grid", in_project);
    if (ref_mpi_once(ref_mpi)) printf("part mesh %s\n", filename);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, filename), "part");
    ref_mpi = ref_grid_mpi(ref_grid); /* ref_grid made a deep copy */
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  } else {
    snprintf(filename, 1024, "%s.meshb", in_project);
    if (ref_mpi_once(ref_mpi)) printf("part mesh %s\n", filename);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, filename), "part");
    ref_mpi = ref_grid_mpi(ref_grid); /* ref_grid made a deep copy */
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  RXS(ref_args_find(argc, argv, "-t", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos)
    RSS(ref_gather_tec_movie_record_button(ref_grid_gather(ref_grid), REF_TRUE),
        "movie on");

  RXS(ref_args_find(argc, argv, "--partitioner", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    REF_INT part_int = atoi(argv[pos + 1]);
    ref_grid_partitioner(ref_grid) = (REF_MIGRATE_PARTIONER)part_int;
    if (ref_mpi_once(ref_mpi))
      printf("--partitioner %d partitioner\n",
             (int)ref_grid_partitioner(ref_grid));
  }

  RXS(ref_args_find(argc, argv, "--quad", &pos), REF_NOT_FOUND, "arg search");
  if (ref_grid_twod(ref_grid) && REF_EMPTY != pos) {
    form_quads = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--quad form quads on boundary\n");
  }

  RXS(ref_args_find(argc, argv, "--ratio-method", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    ref_grid_node(ref_grid)->ratio_method = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf("--ratio-method %d\n", ref_grid_node(ref_grid)->ratio_method);
  }

  RXS(ref_args_find(argc, argv, "--zip-pcurve", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    ref_geom_zip_pcurve(ref_grid_geom(ref_grid)) = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--zip-pcurve pcurve zipping\n");
  }

  RXS(ref_args_find(argc, argv, "--topo", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    ref_grid_adapt(ref_grid, watch_topo) = REF_TRUE;
    if (ref_mpi_once(ref_mpi)) printf("--topo checks active\n");
  }

  RXS(ref_args_find(argc, argv, "--meshlink", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) printf("meshlink with %s\n", argv[pos + 1]);
    RSS(ref_meshlink_open(ref_grid, argv[pos + 1]), "meshlink init");
    RSS(ref_meshlink_infer_orientation(ref_grid), "meshlink orient");
  } else {
    RXS(ref_args_char(argc, argv, "--egads", "-g", &in_egads), REF_NOT_FOUND,
        "egads arg search");
    if (NULL != in_egads) {
      if (ref_mpi_once(ref_mpi)) printf("load egads from %s\n", in_egads);
      RSS(ref_egads_load(ref_grid_geom(ref_grid), in_egads), "load egads");
      if (ref_mpi_once(ref_mpi) && ref_geom_effective(ref_grid_geom(ref_grid)))
        printf("EBody Effective Body loaded\n");
      ref_mpi_stopwatch_stop(ref_mpi, "load egads");
    } else {
      if (0 < ref_geom_cad_data_size(ref_grid_geom(ref_grid))) {
        if (ref_mpi_once(ref_mpi))
          printf("load egadslite from .meshb byte stream\n");
        RSS(ref_egads_load(ref_grid_geom(ref_grid), NULL), "load egads");
        if (ref_mpi_once(ref_mpi) &&
            ref_geom_effective(ref_grid_geom(ref_grid)))
          printf("EBody Effective Body loaded\n");
        ref_mpi_stopwatch_stop(ref_mpi, "load egadslite cad data");
      } else {
        if (ref_mpi_once(ref_mpi))
          printf("warning: no geometry loaded, assuming planar faces.\n");
      }
    }
  }

  RXS(ref_args_find(argc, argv, "--facelift", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) printf("--facelift %s import\n", argv[pos + 1]);
    RSS(ref_facelift_import(ref_grid, argv[pos + 1]), "attach");
    ref_mpi_stopwatch_stop(ref_mpi, "facelift loaded");
  }

  RXS(ref_args_find(argc, argv, "--surrogate", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    if (ref_mpi_once(ref_mpi)) printf("--surrogate %s import\n", argv[pos + 1]);
    RSS(ref_facelift_surrogate(ref_grid, argv[pos + 1]), "attach");
    ref_mpi_stopwatch_stop(ref_mpi, "facelift loaded");
    if (ref_mpi_once(ref_mpi)) printf("constrain all\n");
    RSS(ref_geom_constrain_all(ref_grid), "constrain");
    ref_mpi_stopwatch_stop(ref_mpi, "constrain param");
    if (ref_mpi_once(ref_mpi)) printf("verify constrained param\n");
    RSS(ref_geom_verify_param(ref_grid), "constrained params");
    ref_mpi_stopwatch_stop(ref_mpi, "verify param");
  }

  snprintf(filename, 1024, "%s%s", in_project, soln_import_extension);
  if (ref_mpi_once(ref_mpi)) printf("part scalar %s\n", filename);
  RSS(ref_part_scalar(ref_grid, &ldim, &initial_field, filename),
      "part scalar");
  ref_mpi_stopwatch_stop(ref_mpi, "part scalar");

  if (ref_grid_twod(ref_grid) &&
      0 != strcmp(soln_import_extension, locichem_soln) &&
      0 != strcmp(soln_import_extension, avm_soln)) {
    if (ref_mpi_once(ref_mpi)) printf("flip initial_field v-w for twod\n");
    RSS(ref_phys_flip_twod_yz(ref_grid_node(ref_grid), ldim, initial_field),
        "flip");
  }

  RXS(ref_args_find(argc, argv, "--fixed-point", &fixed_point_pos),
      REF_NOT_FOUND, "arg search");
  RXS(ref_args_find(argc, argv, "--deforming", &deforming_pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != deforming_pos && REF_EMPTY == fixed_point_pos) {
    if (ref_mpi_once(ref_mpi)) printf("extract xyz displacement\n");
    RSS(extract_displaced_xyz(ref_grid_node(ref_grid), &ldim, &initial_field,
                              &displaced),
        "extract displacments");
  }

  ref_malloc_init(metric, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL,
                  0.0);

  multiscale_metric = REF_TRUE;
  RXS(ref_args_find(argc, argv, "--opt-goal", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    multiscale_metric = REF_FALSE;
    if (ref_mpi_once(ref_mpi)) printf("--opt-goal metric construction\n");
    RSS(mask_strong_bc_adjoint(ref_grid, ref_dict_bcs, ldim, initial_field),
        "maks");
    RSS(ref_metric_belme_gfe(metric, ref_grid, ldim, initial_field,
                             reconstruction),
        "add nonlinear terms");
    RSS(ref_recon_roundoff_limit(metric, ref_grid),
        "floor metric eigenvalues based on grid size and solution jitter");
    RSS(ref_metric_local_scale(metric, ref_grid, p), "local scale lp norm");
    RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                           complexity),
        "gradation at complexity");
    RSS(remove_initial_field_adjoint(ref_grid_node(ref_grid), &ldim,
                                     &initial_field),
        "rm adjoint");
  }
  RXS(ref_args_find(argc, argv, "--cons-euler", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_DBL *g;
    multiscale_metric = REF_FALSE;
    if (ref_mpi_once(ref_mpi)) printf("--cons-euler metric construction\n");
    RSS(mask_strong_bc_adjoint(ref_grid, ref_dict_bcs, ldim, initial_field),
        "maks");
    ref_malloc_init(g, 5 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL, 0.0);
    RSS(ref_metric_cons_euler_g(g, ref_grid, ldim, initial_field,
                                reconstruction),
        "cons euler g weights");
    RSS(ref_metric_cons_assembly(metric, g, ref_grid, ldim, initial_field,
                                 reconstruction),
        "cons metric assembly");
    ref_free(g);
    RSS(ref_recon_roundoff_limit(metric, ref_grid),
        "floor metric eigenvalues based on grid size and solution jitter");
    RSS(ref_metric_local_scale(metric, ref_grid, p), "local scale lp norm");
    RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                           complexity),
        "gradation at complexity");
    RSS(remove_initial_field_adjoint(ref_grid_node(ref_grid), &ldim,
                                     &initial_field),
        "rm adjoint");
  }
  RXS(ref_args_find(argc, argv, "--cons-visc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos + 3 < argc) {
    REF_DBL *g;
    REF_DBL mach, re, temperature;
    multiscale_metric = REF_FALSE;
    mach = atof(argv[pos + 1]);
    re = atof(argv[pos + 2]);
    temperature = atof(argv[pos + 3]);
    if (ref_mpi_once(ref_mpi))
      printf(
          "--cons-visc %.3f Mach %.2e Re %.2f temperature metric "
          "construction\n",
          mach, re, temperature);
    RSS(mask_strong_bc_adjoint(ref_grid, ref_dict_bcs, ldim, initial_field),
        "maks");
    ref_malloc_init(g, 5 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL, 0.0);
    RSS(ref_metric_cons_euler_g(g, ref_grid, ldim, initial_field,
                                reconstruction),
        "cons euler g weights");
    RSS(ref_metric_cons_viscous_g(g, ref_grid, ldim, initial_field, mach, re,
                                  temperature, reconstruction),
        "cons viscous g weights");
    RSS(ref_metric_cons_assembly(metric, g, ref_grid, ldim, initial_field,
                                 reconstruction),
        "cons metric assembly");
    ref_free(g);
    RSS(ref_recon_roundoff_limit(metric, ref_grid),
        "floor metric eigenvalues based on grid size and solution jitter");
    RSS(ref_metric_local_scale(metric, ref_grid, p), "local scale lp norm");
    RSS(ref_metric_limit_aspect_ratio(metric, ref_grid, aspect_ratio),
        "limit AR");
    RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                           complexity),
        "gradation at complexity");
    RSS(remove_initial_field_adjoint(ref_grid_node(ref_grid), &ldim,
                                     &initial_field),
        "rm adjoint");
  }
  RXS(ref_args_find(argc, argv, "--fixed-point", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos + 4 < argc) {
    REF_INT first_timestep, last_timestep, timestep_increment;
    const char *solb_middle;
    multiscale_metric = REF_FALSE;
    solb_middle = argv[pos + 1];
    first_timestep = atoi(argv[pos + 2]);
    timestep_increment = atoi(argv[pos + 3]);
    last_timestep = atoi(argv[pos + 4]);
    if (ref_mpi_once(ref_mpi)) {
      printf("--fixed-point\n");
      printf("    %s%s solb project\n", in_project, solb_middle);
      printf("    timesteps [%d ... %d ... %d]\n", first_timestep,
             timestep_increment, last_timestep);
    }
    RXS(ref_args_find(argc, argv, "--deforming", &deforming_pos), REF_NOT_FOUND,
        "arg search");
    if (REF_EMPTY == deforming_pos) {
      REF_BOOL ddes = REF_FALSE;
      RXS(ref_args_find(argc, argv, "--ddes", &pos), REF_NOT_FOUND,
          "arg search");
      if (REF_EMPTY != pos) ddes = REF_TRUE;
      if (ddes) {
        REF_DBL mach, reynolds_number;
        RAS(pos + 2 < argc, "--ddes <Mach> <Reynolds number> missing argument");
        mach = atof(argv[pos + 1]);
        reynolds_number = atof(argv[pos + 2]);
        RSS(ddes_fixed_point_metric(
                metric, ref_grid, first_timestep, last_timestep,
                timestep_increment, in_project, solb_middle, reconstruction, p,
                gradation, complexity, ref_dict_bcs, ldim, initial_field, mach,
                reynolds_number, aspect_ratio),
            "ddes fixed point");
      } else {
        RSS(fixed_point_metric(metric, ref_grid, first_timestep, last_timestep,
                               timestep_increment, in_project, solb_middle,
                               reconstruction, p, gradation, complexity,
                               aspect_ratio, strong_sensor_bc, strong_value,
                               ref_dict_bcs),
            "fixed point");
      }
    } else {
      RSS(moving_fixed_point_metric(metric, ref_grid, first_timestep,
                                    last_timestep, timestep_increment,
                                    in_project, solb_middle, reconstruction, p,
                                    gradation, complexity),
          "moving fixed point");
    }
  }
  if (multiscale_metric) {
    ref_malloc(scalar, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    if (ref_mpi_once(ref_mpi))
      printf("computing interpolant %s for multiscale metric\n", interpolant);
    if (0 == strcmp(soln_import_extension, locichem_soln)) {
      if (ref_mpi_once(ref_mpi)) printf("assuming Loci/CHEM format\n");
      RSS(locichem_field_scalar(ref_grid, ldim, initial_field, interpolant,
                                scalar),
          "Loci/CHEM scalar field reduction");
    } else if (0 == strcmp(soln_import_extension, avm_soln)) {
      if (ref_mpi_once(ref_mpi)) printf("assuming AV (COFFE) format\n");
      RSS(avm_field_scalar(ref_grid, ldim, initial_field, interpolant, scalar),
          "AV scalar field reduction");
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("assuming FUN3D equivalent format and nondimensional\n");
      RSS(fun3d_field_scalar(ref_grid, ldim, initial_field, interpolant,
                             scalar),
          "FUN3D scalar field reduction");
    }

    if (strong_sensor_bc) {
      RSS(ref_phys_strong_sensor_bc(ref_grid, scalar, strong_value,
                                    ref_dict_bcs),
          "apply strong sensor bc");
    }

    RXS(ref_args_find(argc, argv, "--deforming", &pos), REF_NOT_FOUND,
        "arg search");
    if (REF_EMPTY != pos) {
      if (ref_mpi_once(ref_mpi))
        printf("reconstruct Hessian, compute metric\n");
      RSS(ref_metric_moving_multiscale(metric, ref_grid, displaced, scalar,
                                       reconstruction, p, gradation,
                                       complexity),
          "lp norm");
      ref_mpi_stopwatch_stop(ref_mpi, "deforming metric");
    } else {
      RXS(ref_args_find(argc, argv, "--mixed", &pos), REF_NOT_FOUND,
          "arg search");
      if (REF_EMPTY != pos) {
        if (ref_mpi_once(ref_mpi))
          printf("reconstruct Hessian, metric from sensor and infer mixed\n");
        RSS(ref_metric_lp_mixed(metric, ref_grid, scalar, reconstruction, p,
                                gradation, complexity),
            "lp norm");
        ref_mpi_stopwatch_stop(ref_mpi, "mixed metric");
      } else {
        if (ref_mpi_once(ref_mpi))
          printf("reconstruct Hessian, compute metric\n");
        RSS(ref_metric_lp(metric, ref_grid, scalar, reconstruction, p,
                          aspect_ratio, gradation, complexity),
            "lp norm");
        ref_mpi_stopwatch_stop(ref_mpi, "multiscale metric");
        RSS(ref_subcommand_report_error(metric, ref_grid, scalar,
                                        reconstruction, complexity),
            "report error");
      }
    }
    ref_free(scalar);
  }

  RXS(ref_args_find(argc, argv, "--yplus", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    REF_DBL mach, re, temperature, target;
    REF_BOOL sample_viscous_length_error = REF_FALSE;
    RAS(pos + 4 < argc, "--yplus <mach> <re> <temp_k> <target>");
    mach = atof(argv[pos + 1]);
    re = atof(argv[pos + 2]);
    temperature = atof(argv[pos + 3]);
    target = atof(argv[pos + 4]);
    if (ref_mpi_once(ref_mpi)) {
      printf("--yplus %.3f %.2e %.2f %.2f \n<mach> <re> <temp_k> <target>\n",
             mach, re, temperature, target);
    }
    RXS(ref_args_find(argc, argv, "--error", &pos), REF_NOT_FOUND,
        "arg search");
    if (REF_EMPTY != pos) {
      sample_viscous_length_error = REF_TRUE;
    }
    RSS(ref_phys_yplus_metric(ref_grid, metric, mach, re, temperature, target,
                              ldim, initial_field, ref_dict_bcs,
                              sample_viscous_length_error),
        "yplus metric");
  }

  RXS(ref_args_find(argc, argv, "--ypluslen", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_DBL mach, re, temperature, target, reference_length;
    RAS(pos + 5 < argc,
        "--yplus <mach> <re> <temp_k> <target> <reference length>");
    mach = atof(argv[pos + 1]);
    re = atof(argv[pos + 2]);
    temperature = atof(argv[pos + 3]);
    target = atof(argv[pos + 4]);
    reference_length = atof(argv[pos + 5]);
    if (ref_mpi_once(ref_mpi)) {
      printf(
          "--ypluslen %.3f %.2e %.2f %.2f %.2f\n<mach> <re> <temp_k> <target>  "
          "<reference length>\n",
          mach, re, temperature, target, reference_length);
    }
    RSS(ref_phys_yplus_metric_reference_length(
            ref_grid, metric, mach, re, temperature, target, reference_length,
            ldim, initial_field, ref_dict_bcs),
        "yplus metric reference length");
  }

  if (buffer) {
    if (ref_mpi_once(ref_mpi)) printf("buffer at complexity %e\n", complexity);
    RSS(ref_metric_buffer_at_complexity(metric, ref_grid, complexity),
        "buffer at complexity");
    ref_mpi_stopwatch_stop(ref_mpi, "buffer");
  }

  RXS(ref_args_find(argc, argv, "--uniform", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    RSS(ref_metric_parse(metric, ref_grid, argc, argv), "parse uniform");
  }

  RSS(ref_metric_to_node(metric, ref_grid_node(ref_grid)), "set node");
  ref_free(metric);

  RXS(ref_args_find(argc, argv, "--export-metric", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    snprintf(filename, 1024, "%s-metric.solb", in_project);
    if (ref_mpi_once(ref_mpi)) printf("export metric to %s\n", filename);
    RSS(ref_gather_metric(ref_grid, filename), "export metric");
    ref_mpi_stopwatch_stop(ref_mpi, "export metric");
  }

  if (ref_geom_model_loaded(ref_grid_geom(ref_grid))) {
    ref_grid_surf(ref_grid) = ref_grid_twod(ref_grid);
    RSS(ref_egads_mark_jump_degen(ref_grid), "T and UV jumps; UV degen");
  }
  if (ref_geom_model_loaded(ref_grid_geom(ref_grid)) ||
      ref_geom_meshlinked(ref_grid_geom(ref_grid))) {
    RSS(ref_geom_verify_topo(ref_grid), "geom topo");
    RSS(ref_geom_verify_param(ref_grid), "geom param");
    ref_mpi_stopwatch_stop(ref_mpi, "geom assoc");
    RSS(ref_metric_constrain_curvature(ref_grid), "crv const");
    RSS(ref_validation_cell_volume(ref_grid), "vol");
    ref_mpi_stopwatch_stop(ref_mpi, "crv const");
  }
  RSS(ref_grid_cache_background(ref_grid), "cache");
  RSS(ref_node_store_aux(ref_grid_node(ref_grid_background(ref_grid)), ldim,
                         initial_field),
      "store init field with background");
  ref_free(initial_field);
  ref_mpi_stopwatch_stop(ref_mpi, "cache background metric and field");

  RSS(ref_migrate_to_balance(ref_grid), "balance");
  RSS(ref_grid_pack(ref_grid), "pack");
  ref_mpi_stopwatch_stop(ref_mpi, "pack");

  for (pass = 0; !all_done && pass < passes; pass++) {
    if (ref_mpi_once(ref_mpi))
      printf("\n pass %d of %d with %d ranks\n", pass + 1, passes,
             ref_mpi_n(ref_mpi));
    if (form_quads && pass == passes / 2) {
      if (ref_mpi_once(ref_mpi)) printf("form quads\n");
      RSS(ref_layer_align_quad(ref_grid), "quad");
    }
    all_done1 = all_done0;
    RSS(ref_adapt_pass(ref_grid, &all_done0), "pass");
    all_done = all_done0 && all_done1 && (pass > MIN(5, passes));
    RSS(ref_metric_synchronize(ref_grid), "sync with background");
    ref_mpi_stopwatch_stop(ref_mpi, "metric sync");
    RSS(ref_validation_cell_volume(ref_grid), "vol");
    RSS(ref_adapt_tattle_faces(ref_grid), "tattle");
    ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "tattle faces");
    RSS(ref_migrate_to_balance(ref_grid), "balance");
    RSS(ref_grid_pack(ref_grid), "pack");
    ref_mpi_stopwatch_stop(ref_mpi, "pack");
  }

  RXS(ref_args_find(argc, argv, "--usm3d", &pos), REF_NOT_FOUND, "parse usm3d");
  if (REF_EMPTY != pos) {
    RSS(ref_egads_enforce_y_symmetry(ref_grid), "RSS");
    RSS(ref_validation_cell_volume(ref_grid), "vol");
  }

  RSS(ref_node_implicit_global_from_local(ref_grid_node(ref_grid)),
      "implicit global");
  ref_mpi_stopwatch_stop(ref_mpi, "implicit global");

  RSS(ref_geom_verify_param(ref_grid), "final params");
  ref_mpi_stopwatch_stop(ref_mpi, "verify final params");

  RXS(ref_args_find(argc, argv, "--export-metric", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    snprintf(filename, 1024, "%s-final-metric.solb", out_project);
    if (ref_mpi_once(ref_mpi)) printf("export metric to %s\n", filename);
    RSS(ref_gather_metric(ref_grid, filename), "export metric");
    ref_mpi_stopwatch_stop(ref_mpi, "export metric");
  }

  snprintf(filename, 1024, "%s.meshb", out_project);
  if (ref_mpi_once(ref_mpi))
    printf("gather " REF_GLOB_FMT " nodes to %s\n",
           ref_node_n_global(ref_grid_node(ref_grid)), filename);
  RSS(ref_gather_by_extension(ref_grid, filename), "gather .meshb");
  ref_mpi_stopwatch_stop(ref_mpi, "gather meshb");

  snprintf(filename, 1024, "%s.%s", out_project, mesh_export_extension);
  if (0 != strcmp(soln_export_extension, i_like_restart) &&
      0 != strcmp(soln_export_extension, avm_restart) &&
      ref_grid_twod(ref_grid)) {
    if (ref_mpi_once(ref_mpi)) printf("extrude twod\n");
    RSS(ref_grid_extrude_twod(&extruded_grid, ref_grid, 2), "extrude");
    RXS(ref_args_find(argc, argv, "--axi", &pos), REF_NOT_FOUND, "arg search");
    if (REF_EMPTY != pos) {
      if (ref_mpi_once(ref_mpi)) printf(" --axi convert extrusion to wedge.\n");
      RSS(ref_axi_wedge(extruded_grid), "axi wedge");
    }
    if (ref_mpi_once(ref_mpi))
      printf("gather extruded " REF_GLOB_FMT " nodes to %s\n",
             ref_node_n_global(ref_grid_node(extruded_grid)), filename);
    if (ref_mpi_once(ref_mpi)) printf("gather extruded %s\n", filename);
    RSS(ref_gather_by_extension(extruded_grid, filename),
        "gather mesh extension");
  } else {
    if (ref_mpi_once(ref_mpi))
      printf("gather " REF_GLOB_FMT " nodes to %s\n",
             ref_node_n_global(ref_grid_node(ref_grid)), filename);
    if (ref_mpi_para(ref_mpi)) {
      RSS(ref_gather_by_extension(ref_grid, filename), "gather mesh extension");
    } else {
      RSS(ref_export_by_extension(ref_grid, filename), "export mesh extension");
    }
  }
  ref_mpi_stopwatch_stop(ref_mpi, "gather mesh extension");

  if (ref_mpi_once(ref_mpi)) {
    printf("%d leading dim from " REF_GLOB_FMT " donor nodes to " REF_GLOB_FMT
           " receptor nodes\n",
           ldim,
           ref_node_n_global(ref_grid_node(ref_grid_background(ref_grid))),
           ref_node_n_global(ref_grid_node(ref_grid)));
  }

  if (ref_mpi_once(ref_mpi)) printf("interpolate receptor nodes\n");
  ref_malloc_init(ref_field, ldim * ref_node_max(ref_grid_node(ref_grid)),
                  REF_DBL, 0.0);
  RSS(ref_node_extract_aux(ref_grid_node(ref_grid_background(ref_grid)), &ldim,
                           &initial_field),
      "store init field with background");
  RSS(ref_validation_finite(ref_grid_background(ref_grid), ldim, initial_field),
      "recall background field");

  RSS(ref_interp_scalar(ref_grid_interp(ref_grid), ldim, initial_field,
                        ref_field),
      "interp scalar");
  RSS(ref_validation_finite(ref_grid, ldim, ref_field), "interp field");
  ref_free(initial_field);
  /* free interp and background grid */
  RSS(ref_grid_free(ref_grid_background(ref_grid)),
      "free cached background grid");
  RSS(ref_interp_free(ref_grid_interp(ref_grid)), "interp free");
  ref_grid_interp(ref_grid) = NULL;
  ref_mpi_stopwatch_stop(ref_mpi, "interp");

  if (ref_grid_twod(ref_grid) &&
      0 != strcmp(soln_import_extension, locichem_soln) &&
      0 != strcmp(soln_import_extension, avm_soln)) {
    if (ref_mpi_once(ref_mpi)) printf("flip ref_field v-w for twod\n");
    RSS(ref_phys_flip_twod_yz(ref_grid_node(ref_grid), ldim, ref_field),
        "flip");
  }

  snprintf(filename, 1024, "%s%s", out_project, soln_export_extension);
  if (NULL != extruded_grid) {
    if (ref_mpi_once(ref_mpi)) printf("extruding field of %d\n", ldim);
    ref_malloc(extruded_field,
               ldim * ref_node_max(ref_grid_node(extruded_grid)), REF_DBL);
    RSS(ref_grid_extrude_field(ref_grid, ldim, ref_field, extruded_grid,
                               extruded_field),
        "extrude field");
    RSS(ref_validation_finite(extruded_grid, ldim, extruded_field),
        "extruded field");
    if (usm3d_restart != soln_export_extension) {
      if (ref_mpi_once(ref_mpi))
        printf("writing interpolated extruded field %s\n", filename);
      RSS(ref_gather_scalar_by_extension(extruded_grid, ldim, extruded_field,
                                         NULL, filename),
          "gather recept");
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("writing interpolated field at prism cell centers %s\n",
               filename);
      RSS(ref_gather_scalar_cell_solb(extruded_grid, ldim, extruded_field,
                                      filename),
          "gather cell center");
    }
    ref_free(extruded_field);
    ref_grid_free(extruded_grid);
  } else {
    if (usm3d_restart != soln_export_extension) {
      if (ref_mpi_once(ref_mpi))
        printf("writing interpolated field %s\n", filename);
      RSS(ref_gather_scalar_by_extension(ref_grid, ldim, ref_field, NULL,
                                         filename),
          "gather recept");
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("writing interpolated field at tet cell centers %s\n", filename);
      RSS(ref_gather_scalar_cell_solb(ref_grid, ldim, ref_field, filename),
          "gather cell center");
    }
  }
  ref_mpi_stopwatch_stop(ref_mpi, "gather receptor");

  ref_free(ref_field);

  /* export via -x grid.ext and -f final-surf.tec*/
  for (pos = 0; pos < argc - 1; pos++) {
    if (strcmp(argv[pos], "-x") == 0) {
      if (ref_mpi_para(ref_mpi)) {
        if (ref_mpi_once(ref_mpi))
          printf("gather " REF_GLOB_FMT " nodes to %s\n",
                 ref_node_n_global(ref_grid_node(ref_grid)), argv[pos + 1]);
        RSS(ref_gather_by_extension(ref_grid, argv[pos + 1]), "gather -x");
      } else {
        if (ref_mpi_once(ref_mpi))
          printf("export " REF_GLOB_FMT " nodes to %s\n",
                 ref_node_n_global(ref_grid_node(ref_grid)), argv[pos + 1]);
        RSS(ref_export_by_extension(ref_grid, argv[pos + 1]), "export -x");
      }
    }
    if (strcmp(argv[pos], "-f") == 0) {
      if (ref_mpi_once(ref_mpi))
        printf("gather final surface status %s\n", argv[pos + 1]);
      RSS(ref_gather_surf_status_tec(ref_grid, argv[pos + 1]), "gather -f");
    }
  }

  RSS(ref_dict_free(ref_dict_bcs), "free");
  RSS(ref_grid_free(ref_grid), "free");

  return REF_SUCCESS;
shutdown:
  return REF_FAILURE;
}

static REF_STATUS hessian_multiscale(REF_MPI ref_mpi, REF_GRID ref_grid,
                                     const char *in_scalar, REF_DBL *metric,
                                     REF_INT p, REF_DBL gradation,
                                     REF_DBL complexity) {
  if (ref_mpi_once(ref_mpi)) printf("part hessian %s\n", in_scalar);
  RSS(ref_part_metric(ref_grid_node(ref_grid), in_scalar), "part scalar");
  ref_mpi_stopwatch_stop(ref_mpi, "part metric");
  RSS(ref_metric_from_node(metric, ref_grid_node(ref_grid)), "get node");
  RSS(ref_recon_abs_value_hessian(ref_grid, metric), "abs val");
  RSS(ref_recon_roundoff_limit(metric, ref_grid),
      "floor metric eigenvalues based on grid size and solution jitter");
  RSS(ref_metric_local_scale(metric, ref_grid, p), "local scale lp norm");
  RSS(ref_metric_gradation_at_complexity(metric, ref_grid, gradation,
                                         complexity),
      "gradation at complexity");
  ref_mpi_stopwatch_stop(ref_mpi, "compute metric from hessian");
  return REF_SUCCESS;
}

static REF_STATUS multiscale(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *out_metric;
  char *in_mesh;
  char *in_scalar;
  REF_GRID ref_grid = NULL;
  REF_INT ldim;
  REF_DBL *scalar = NULL;
  REF_DBL *metric = NULL;
  REF_INT p;
  REF_DBL gradation, complexity, current_complexity;
  REF_RECON_RECONSTRUCTION reconstruction = REF_RECON_L2PROJECTION;
  REF_INT pos;
  REF_INT hessian_pos, fixed_point_pos;
  REF_DBL aspect_ratio = -1.0;
  REF_DICT ref_dict_bcs = NULL;

  if (argc < 6) goto shutdown;
  in_mesh = argv[2];
  in_scalar = argv[3];
  complexity = atof(argv[4]);
  out_metric = argv[5];

  p = 2;
  RXS(ref_args_find(argc, argv, "--norm-power", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (pos >= argc - 1) {
      if (ref_mpi_once(ref_mpi))
        printf("option missing value: --norm-power <norm power>\n");
      goto shutdown;
    }
    p = atoi(argv[pos + 1]);
  }

  gradation = -1.0;
  RXS(ref_args_find(argc, argv, "--gradation", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (pos >= argc - 1) {
      if (ref_mpi_once(ref_mpi))
        printf("option missing value: --gradation <gradation>\n");
      goto shutdown;
    }
    gradation = atof(argv[pos + 1]);
  }

  RXS(ref_args_find(argc, argv, "--hessian", &hessian_pos), REF_NOT_FOUND,
      "arg search");
  RXS(ref_args_find(argc, argv, "--fixed-point", &fixed_point_pos),
      REF_NOT_FOUND, "arg search");

  aspect_ratio = -1.0;
  RXS(ref_args_find(argc, argv, "--aspect-ratio", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (pos >= argc - 1) {
      if (ref_mpi_once(ref_mpi))
        printf("option missing value: --aspect-ratio <aspect-ratio>\n");
      goto shutdown;
    }
    aspect_ratio = atof(argv[pos + 1]);
  }

  RSS(ref_dict_create(&ref_dict_bcs), "make dict");
  RXS(ref_args_find(argc, argv, "--fun3d-mapbc", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *mapbc;
    mapbc = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("reading fun3d bc map %s\n", mapbc);
      RSS(ref_phys_read_mapbc(ref_dict_bcs, mapbc),
          "unable to read fun3d formatted mapbc");
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  RXS(ref_args_find(argc, argv, "--viscous-tags", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    const char *tags;
    tags = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) {
      printf("parsing viscous tags\n");
      RSS(ref_phys_parse_tags(ref_dict_bcs, tags),
          "unable to parse viscous tags");
      printf(" %d viscous tags parsed\n", ref_dict_n(ref_dict_bcs));
    }
    RSS(ref_dict_bcast(ref_dict_bcs, ref_mpi), "bcast");
  }

  if (ref_mpi_once(ref_mpi)) {
    printf("complexity %f\n", complexity);
    printf("Lp=%d\n", p);
    printf("gradation %f\n", gradation);
    printf("reconstruction %d\n", (int)reconstruction);
  }
  RAS(complexity > 1.0e-20, "complexity must be greater than zero");

  ref_mpi_stopwatch_start(ref_mpi);

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", in_mesh);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, in_mesh), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", in_mesh);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_mesh), "import");
    ref_mpi_stopwatch_stop(ref_mpi, "import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  ref_malloc(metric, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);

  if (REF_EMPTY != fixed_point_pos) {
    REF_INT first_timestep, last_timestep, timestep_increment;
    const char *solb_middle;
    const char in_project[] = "";
    REF_BOOL strong_sensor_bc = REF_FALSE;
    REF_DBL strong_value = 0.0;
    solb_middle = argv[fixed_point_pos + 1];
    first_timestep = atoi(argv[fixed_point_pos + 2]);
    timestep_increment = atoi(argv[fixed_point_pos + 3]);
    last_timestep = atoi(argv[fixed_point_pos + 4]);
    RXS(ref_args_find(argc, argv, "--strong-sensor-bc", &pos), REF_NOT_FOUND,
        "arg search");
    if (REF_EMPTY != pos) {
      RAS(pos + 1 < argc, "--strong-sensor-bc <value>");
      strong_sensor_bc = REF_TRUE;
      strong_value = atof(argv[pos + 1]);
    }
    if (ref_mpi_once(ref_mpi)) {
      printf("--fixed-point\n");
      printf("    %s%s solb project\n", in_project, solb_middle);
      printf("    timesteps [%d ... %d ... %d]\n", first_timestep,
             timestep_increment, last_timestep);
    }
    RSS(fixed_point_metric(
            metric, ref_grid, first_timestep, last_timestep, timestep_increment,
            in_project, solb_middle, reconstruction, p, gradation, complexity,
            aspect_ratio, strong_sensor_bc, strong_value, ref_dict_bcs),
        "fixed point");
  } else if (REF_EMPTY != hessian_pos) {
    RSS(hessian_multiscale(ref_mpi, ref_grid, in_scalar, metric, p, gradation,
                           complexity),
        "hessian multiscale");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("part scalar %s\n", in_scalar);
    RSS(ref_part_scalar(ref_grid, &ldim, &scalar, in_scalar), "part scalar");
    REIS(1, ldim, "expected one scalar");
    ref_mpi_stopwatch_stop(ref_mpi, "part scalar");
    
    if (ref_mpi_once(ref_mpi)) printf("reconstruct Hessian, compute metric\n");
    RSS(ref_metric_lp(metric, ref_grid, scalar, reconstruction, p, gradation,
                      aspect_ratio, complexity),
        "lp norm");
    ref_mpi_stopwatch_stop(ref_mpi, "compute metric");
    RSS(ref_subcommand_report_error(metric, ref_grid, scalar, reconstruction,
                                    complexity),
        "report error");
    ref_free(scalar);
  }

  RXS(ref_args_find(argc, argv, "--buffer", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) printf("buffer at complexity %e\n", complexity);
    RSS(ref_metric_buffer_at_complexity(metric, ref_grid, complexity),
        "buffer at complexity");
    ref_mpi_stopwatch_stop(ref_mpi, "buffer");
  }

  RXS(ref_args_find(argc, argv, "--uniform", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    RSS(ref_metric_parse(metric, ref_grid, argc, argv), "parse uniform");
  }

  RSS(ref_metric_complexity(metric, ref_grid, &current_complexity), "cmp");
  if (ref_mpi_once(ref_mpi))
    printf("actual complexity %e\n", current_complexity);
  RSS(ref_metric_to_node(metric, ref_grid_node(ref_grid)), "set node");

  RXS(ref_args_find(argc, argv, "--pcd", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos + 1 < argc) {
    REF_DBL *hh;
    const char *title[] = {"spacing", "decay"};
    ref_malloc(hh, 2 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    RSS(ref_metric_isotropic(metric, ref_grid, hh), "iso");
    ref_mpi_stopwatch_stop(ref_mpi, "isotropic");
    if (ref_mpi_once(ref_mpi)) printf("gather %s\n", argv[pos + 1]);
    RSS(ref_gather_scalar_by_extension(ref_grid, 2, hh, title, argv[pos + 1]),
        "dump hh");
    ref_free(hh);
  }

  ref_free(metric);

  if (ref_mpi_once(ref_mpi)) printf("gather %s\n", out_metric);
  RSS(ref_gather_metric(ref_grid, out_metric), "gather metric");
  ref_mpi_stopwatch_stop(ref_mpi, "gather metric");

  RSS(ref_dict_free(ref_dict_bcs), "free");
  RSS(ref_grid_free(ref_grid), "free grid");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) multiscale_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS node(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *in_file;
  REF_INT pos, global, local;
  REF_GRID ref_grid = NULL;

  if (ref_mpi_para(ref_mpi)) {
    RSS(REF_IMPLEMENT, "ref node is not parallel");
  }
  if (argc < 4) goto shutdown;
  in_file = argv[2];

  printf("import %s\n", in_file);
  RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_file), "load surface");

  for (pos = 3; pos < argc; pos++) {
    global = atoi(argv[pos]);
    printf("global index %d\n", global);
    RSS(ref_node_local(ref_grid_node(ref_grid), global, &local),
        "global node_index not found");
    RSS(ref_node_location(ref_grid_node(ref_grid), local), "location");
  }

  RSS(ref_grid_free(ref_grid), "create");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) quilt_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS quilt(REF_MPI ref_mpi, int argc, char *argv[]) {
  REF_GEOM ref_geom;
  char *input_egads;
  size_t end_of_string;
  char project[1000];
  char output_egads[1024];
  REF_INT pos;
  REF_DBL *global_params = NULL;
  REF_INT auto_tparams = REF_EGADS_RECOMMENDED_TPARAM;

  if (argc < 3) goto shutdown;
  input_egads = argv[2];

  RAS(ref_egads_allows_construction(),
      "EGADS not linked with OpenCASCADE, required to load model")
  RAS(ref_egads_allows_effective(), "EGADS does not support Effective Geometry")

  end_of_string = MIN(1023, strlen(input_egads));
  RAS((7 < end_of_string &&
       strncmp(&(input_egads[end_of_string - 6]), ".egads", 6) == 0),
      ".egads extension missing");
  strncpy(project, input_egads, end_of_string - 6);
  project[end_of_string - 6] = '\0';
  snprintf(output_egads, 1024, "%s-eff.egads", project);

  RXS(ref_args_find(argc, argv, "--global", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 3) {
    ref_malloc(global_params, 3, REF_DBL);
    global_params[0] = atof(argv[pos + 1]);
    global_params[1] = atof(argv[pos + 2]);
    global_params[2] = atof(argv[pos + 3]);
    if (ref_mpi_once(ref_mpi))
      printf("initial tessellation, global param %f %f %f\n", global_params[0],
             global_params[1], global_params[2]);
  } else {
    if (ref_mpi_once(ref_mpi)) printf("initial tessellation, default param\n");
  }

  RXS(ref_args_find(argc, argv, "--auto-tparams", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    auto_tparams = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi))
      printf("--auto-tparams %d requested\n", auto_tparams);
    if (auto_tparams < 0) {
      auto_tparams = REF_EGADS_ALL_TPARAM;
      if (ref_mpi_once(ref_mpi))
        printf("--auto-tparams %d set to all\n", auto_tparams);
    }
  }

  RSS(ref_geom_create(&ref_geom), "create geom");
  RSS(ref_egads_load(ref_geom, input_egads), "load");
  if (ref_mpi_once(ref_mpi) && ref_geom_effective(ref_geom))
    printf("EBody Effective Body loaded\n");
  RSS(ref_egads_quilt(ref_geom, auto_tparams, global_params), "quilt");
  RSS(ref_egads_save(ref_geom, output_egads), "save");
  RSS(ref_geom_free(ref_geom), "free geom/context");

  ref_free(global_params);
  global_params = NULL;

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) quilt_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS translate(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *out_file;
  char *in_file;
  REF_GRID ref_grid = NULL;
  REF_INT pos;
  REF_BOOL extrude = REF_FALSE;
  REF_BOOL surface_only = REF_FALSE;
  size_t end_of_string;

  if (argc < 4) goto shutdown;
  in_file = argv[2];
  out_file = argv[3];

  ref_mpi_stopwatch_start(ref_mpi);

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", in_file);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, in_file), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", in_file);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_file), "import");
    ref_mpi_stopwatch_stop(ref_mpi, "import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  RXS(ref_args_find(argc, argv, "--scale", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    char *endptr;
    REF_DBL scale;
    REF_NODE ref_node = ref_grid_node(ref_grid);
    REF_INT node;
    if (pos + 1 >= argc) {
      if (ref_mpi_once(ref_mpi)) printf("--scale missing scale\n");
      goto shutdown;
    }
    pos++;
    scale = strtod(argv[pos], &endptr);
    RAS(argv[pos] != endptr, "parse scale");
    if (ref_mpi_once(ref_mpi)) printf("--scale %e\n", scale);
    each_ref_node_valid_node(ref_node, node) {
      ref_node_xyz(ref_node, 0, node) *= scale;
      ref_node_xyz(ref_node, 1, node) *= scale;
      ref_node_xyz(ref_node, 2, node) *= scale;
    }
  }

  RXS(ref_args_find(argc, argv, "--shift", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    char *endptr;
    REF_DBL dx, dy, dz;
    REF_NODE ref_node = ref_grid_node(ref_grid);
    REF_INT node;
    if (pos + 3 >= argc) {
      if (ref_mpi_once(ref_mpi)) printf("--shift missing dx dy dz\n");
      goto shutdown;
    }
    pos++;
    dx = strtod(argv[pos], &endptr);
    RAS(argv[pos] != endptr, "parse dx");
    pos++;
    dy = strtod(argv[pos], &endptr);
    RAS(argv[pos] != endptr, "parse dy");
    pos++;
    dz = strtod(argv[pos], &endptr);
    RAS(argv[pos] != endptr, "parse dz");
    if (ref_mpi_once(ref_mpi)) printf("--shift %e %e %e\n", dx, dy, dz);
    each_ref_node_valid_node(ref_node, node) {
      ref_node_xyz(ref_node, 0, node) += dx;
      ref_node_xyz(ref_node, 1, node) += dy;
      ref_node_xyz(ref_node, 2, node) += dz;
    }
  }

  RXS(ref_args_find(argc, argv, "--rotatey", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    char *endptr;
    REF_DBL degree, rad;
    REF_NODE ref_node = ref_grid_node(ref_grid);
    REF_INT node;
    if (pos + 1 >= argc) {
      if (ref_mpi_once(ref_mpi)) printf("--rotatey missing degrees\n");
      goto shutdown;
    }
    pos++;
    degree = strtod(argv[pos], &endptr);
    RAS(argv[pos] != endptr, "parse degree");
    rad = ref_math_in_radians(degree);
    if (ref_mpi_once(ref_mpi))
      printf("--rotatex %f degree %f radian\n", degree, rad);
    each_ref_node_valid_node(ref_node, node) {
      REF_DBL x, y, z;
      x = ref_node_xyz(ref_node, 0, node);
      y = ref_node_xyz(ref_node, 1, node);
      z = ref_node_xyz(ref_node, 2, node);
      ref_node_xyz(ref_node, 0, node) = x * cos(rad) - z * sin(rad);
      ref_node_xyz(ref_node, 1, node) = y;
      ref_node_xyz(ref_node, 2, node) = x * sin(rad) + z * cos(rad);
    }
  }

  RXS(ref_args_find(argc, argv, "--surface", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_INT group;
    REF_CELL ref_cell;
    if (ref_mpi_once(ref_mpi)) printf("  --surface deleting 3D cells\n");
    surface_only = REF_TRUE;
    each_ref_grid_3d_ref_cell(ref_grid, group, ref_cell) {
      RSS(ref_cell_free(ref_cell), "free cell");
      RSS(ref_cell_create(&ref_grid_cell(ref_grid, group),
                          (REF_CELL_TYPE)group),
          "empty cell create");
      ref_cell = ref_grid_cell(ref_grid, group);
    }
  }

  RXS(ref_args_find(argc, argv, "--orient", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) printf("  --orient twod in place\n");
    RSS(ref_grid_orient_twod(ref_grid), "orient twod");
  }

  RXS(ref_args_find(argc, argv, "--shard", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) printf("  --shard in place\n");
    RSS(ref_shard_in_place(ref_grid), "shard to simplex");
  }

  RXS(ref_args_find(argc, argv, "--blockhead", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) printf("  --blockhead in place\n");
    RSS(ref_subdiv_to_hex(ref_grid), "shard to simplex");
  }

  RXS(ref_args_find(argc, argv, "--enrich2", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) printf("  --enrich2\n");
    RSS(ref_geom_enrich2(ref_grid), "enrich to q2");
  }

  RXS(ref_args_find(argc, argv, "--extrude", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    extrude = REF_TRUE;
  }

  RXS(ref_args_find(argc, argv, "--planes", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && extrude) {
    if (ref_mpi_once(ref_mpi)) printf("--extrude and --planes exclusive\n");
    goto shutdown;
  }
  if (REF_EMPTY != pos) {
    REF_GRID twod_grid = ref_grid;
    REF_INT n_planes;
    if (pos + 1 >= argc) {
      if (ref_mpi_once(ref_mpi)) printf("--planes missing N\n");
      goto shutdown;
    }
    n_planes = atoi(argv[pos + 1]);
    if (n_planes < 2) {
      if (ref_mpi_once(ref_mpi))
        printf("--planes %d must be 2 or more\n", n_planes);
      goto shutdown;
    }
    if (ref_mpi_once(ref_mpi))
      printf("extrude %d layers of prisms\n", n_planes);
    RSS(ref_grid_extrude_twod(&ref_grid, twod_grid, n_planes), "extrude");
    RSS(ref_grid_free(twod_grid), "free");
  } else {
    end_of_string = strlen(out_file);
    if (ref_grid_twod(ref_grid) && (end_of_string >= 6) &&
        (strncmp(&out_file[end_of_string - 6], ".ugrid", 6)) == 0) {
      extrude = REF_TRUE;
      if (ref_mpi_once(ref_mpi))
        printf("  --extrude implicitly added to ugrid output of 2D input.\n");
    }
  }

  if (extrude) {
    REF_GRID twod_grid = ref_grid;
    if (ref_mpi_once(ref_mpi)) printf("extrude prisms\n");
    RSS(ref_grid_extrude_twod(&ref_grid, twod_grid, 2), "extrude");
    RSS(ref_grid_free(twod_grid), "free");
  }

  RXS(ref_args_find(argc, argv, "--zero-y-face", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_DBL deviation, total_deviation;
    REF_CELL ref_cell;
    REF_NODE ref_node = ref_grid_node(ref_grid);
    REF_INT faceid, group, cell, node, nodes[REF_CELL_MAX_SIZE_PER];
    if (pos + 1 >= argc) {
      if (ref_mpi_once(ref_mpi)) printf("--zero-y-face missing faceid\n");
      goto shutdown;
    }
    faceid = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi)) printf("zero y of face %d\n", faceid);
    deviation = 0.0;
    each_ref_grid_2d_ref_cell(ref_grid, group, ref_cell) {
      each_ref_cell_valid_cell_with_nodes(ref_cell, cell, nodes) {
        if (faceid == nodes[ref_cell_node_per(ref_cell)]) {
          each_ref_cell_cell_node(ref_cell, node) {
            deviation =
                MAX(deviation, ABS(ref_node_xyz(ref_node, 1, nodes[node])));
            ref_node_xyz(ref_node, 1, nodes[node]) = 0.0;
          }
        }
      }
    }
    RSS(ref_mpi_max(ref_mpi, &deviation, &total_deviation, REF_DBL_TYPE),
        "mpi max");
    if (ref_mpi_once(ref_mpi)) printf("max deviation %e\n", deviation);
  }

  RXS(ref_args_find(argc, argv, "--axi", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos) {
    if (ref_mpi_once(ref_mpi)) printf("--axi creates wedge about y=z=0 axis\n");
    RSS(ref_axi_wedge(ref_grid), "wedge");
  }

  if (ref_mpi_para(ref_mpi)) {
    if (surface_only) {
      if (ref_mpi_once(ref_mpi)) printf("gather surface to %s\n", out_file);
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("gather " REF_GLOB_FMT " nodes to %s\n",
               ref_node_n_global(ref_grid_node(ref_grid)), out_file);
    }
    RSS(ref_gather_by_extension(ref_grid, out_file), "gather");
    ref_mpi_stopwatch_stop(ref_mpi, "gather");
  } else {
    if (surface_only) {
      if (ref_mpi_once(ref_mpi)) printf("export surface to %s\n", out_file);
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("export " REF_GLOB_FMT " nodes to %s\n",
               ref_node_n_global(ref_grid_node(ref_grid)), out_file);
    }
    RSS(ref_export_by_extension(ref_grid, out_file), "export");
    ref_mpi_stopwatch_stop(ref_mpi, "export");
  }

  RSS(ref_grid_free(ref_grid), "free grid");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) translate_help(argv[0]);
  return REF_FAILURE;
}

static REF_STATUS visualize(REF_MPI ref_mpi, int argc, char *argv[]) {
  char *in_mesh;
  char *in_sol;
  char *out_sol;
  REF_GRID ref_grid = NULL;
  REF_INT ldim;
  REF_DBL *field;
  REF_INT pos;
  REF_BOOL surface_only = REF_FALSE;

  if (argc < 5) goto shutdown;
  in_mesh = argv[2];
  in_sol = argv[3];
  out_sol = argv[4];

  ref_mpi_stopwatch_start(ref_mpi);

  if (ref_mpi_para(ref_mpi)) {
    if (ref_mpi_once(ref_mpi)) printf("part %s\n", in_mesh);
    RSS(ref_part_by_extension(&ref_grid, ref_mpi, in_mesh), "part");
    ref_mpi_stopwatch_stop(ref_mpi, "part");
  } else {
    if (ref_mpi_once(ref_mpi)) printf("import %s\n", in_mesh);
    RSS(ref_import_by_extension(&ref_grid, ref_mpi, in_mesh), "import");
    ref_mpi_stopwatch_stop(ref_mpi, "import");
  }
  if (ref_mpi_once(ref_mpi))
    printf("  read " REF_GLOB_FMT " vertices\n",
           ref_node_n_global(ref_grid_node(ref_grid)));

  if (strcmp(in_sol, "none") == 0) {
    field = NULL;
    ldim = 0;
    if (ref_mpi_once(ref_mpi))
      printf("skipping read of %d ldim from %s\n", ldim, in_sol);
  } else if (strcmp(in_sol, "degree") == 0) {
    REF_CELL ref_cell;
    REF_INT node, group, degree;
    ref_malloc_init(field, ref_node_max(ref_grid_node(ref_grid)), REF_DBL, 0.0);
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      if (ref_grid_twod(ref_grid)) {
        each_ref_grid_2d_ref_cell(ref_grid, group, ref_cell) {
          RSS(ref_adj_degree(ref_cell_adj(ref_cell), node, &degree), "deg");
          field[node] += (REF_DBL)degree;
        }
      } else {
        each_ref_grid_3d_ref_cell(ref_grid, group, ref_cell) {
          RSS(ref_adj_degree(ref_cell_adj(ref_cell), node, &degree), "deg");
          field[node] += (REF_DBL)degree;
        }
      }
    }
    ldim = 1;
    if (ref_mpi_once(ref_mpi))
      printf("%d ldim for %s (degree)\n", ldim, in_sol);
  } else if (strcmp(in_sol, "hmin") == 0) {
    REF_INT node;
    REF_DBL *metric, diag[12], hmin, temp_local;
    ref_malloc_init(field, ref_node_max(ref_grid_node(ref_grid)), REF_DBL,
                    REF_DBL_MAX);
    if (ref_mpi_once(ref_mpi)) printf("imply metric from mesh\n");
    ref_malloc(metric, 6 * ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    RSS(ref_metric_imply_from(metric, ref_grid), "imply");
    ref_mpi_stopwatch_stop(ref_mpi, "metric implied");
    hmin = REF_DBL_MAX;
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      RSS(ref_matrix_diag_m(&(metric[6 * node]), diag), "decomp");
      if (ref_grid_twod(ref_grid)) {
        RSS(ref_matrix_descending_eig_twod(diag), "2D ascend");
        if (ref_math_divisible(1.0, sqrt(diag[1])))
          field[node] = 1.0 / sqrt(diag[1]);
      } else {
        RSS(ref_matrix_descending_eig_twod(diag), "2D ascend");
        if (ref_math_divisible(1.0, sqrt(diag[2])))
          field[node] = 1.0 / sqrt(diag[2]);
      }
      hmin = MIN(field[node], hmin);
    }
    temp_local = hmin;
    RSS(ref_mpi_min(ref_mpi, &temp_local, &hmin, REF_DBL_TYPE), "min");
    ref_free(metric);
    ldim = 1;
    if (ref_mpi_once(ref_mpi))
      printf("%d ldim for %s (hmin) = %e\n", ldim, in_sol, hmin);
  } else {
    if (ref_mpi_once(ref_mpi)) printf("read solution %s\n", in_sol);
    RSS(ref_part_scalar(ref_grid, &ldim, &field, in_sol), "scalar");
    if (ref_mpi_once(ref_mpi)) printf("  with leading dimension %d\n", ldim);
    ref_mpi_stopwatch_stop(ref_mpi, "read solution");
  }

  RXS(ref_args_find(argc, argv, "--boom", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos + 4 < argc) {
    REF_INT node, i;
    REF_DBL center[3], aoa, phi, h;
    REF_DBL *dp_pinf;
    FILE *file;
    const char *vars[] = {"dp/pinf"};
    ref_malloc(dp_pinf, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_INT pressure_index = 4;
      REF_DBL gamma = 1.4;
      dp_pinf[node] =
          (field[pressure_index + ldim * node] - 1.0 / gamma) * gamma;
    }
    center[0] = atof(argv[pos + 1]);
    center[1] = atof(argv[pos + 2]);
    center[2] = atof(argv[pos + 3]);
    aoa = atof(argv[pos + 4]);
    if (ref_mpi_once(ref_mpi))
      printf("  center %f %f %f\n", center[0], center[1], center[2]);
    if (ref_mpi_once(ref_mpi)) printf("  angle of attack %f\n", aoa);
    file = NULL;
    if (ref_mpi_once(ref_mpi)) {
      RSS(ref_iso_boom_header(&file, 1, vars, out_sol), "boom header");
    }
    if (ref_mpi_once(ref_mpi)) printf(" open %s\n", out_sol);
    for (i = pos + 5; i + 1 < argc; i += 2) {
      phi = atof(argv[i]);
      h = atof(argv[i + 1]);
      if (ref_mpi_once(ref_mpi)) printf("   phi %f h %f\n", phi, h);
      RSS(ref_iso_boom_zone(file, ref_grid, dp_pinf, 1, center, aoa, phi, h),
          " boom zone");
      ref_mpi_stopwatch_stop(ref_mpi, "export ray");
    }
    ref_free(dp_pinf);
    ref_free(field);
    RSS(ref_grid_free(ref_grid), "free grid");
    return REF_SUCCESS;
  }

  {
    REF_BOOL boom_ray = REF_FALSE;
    REF_DBL *dp_pinf = NULL;
    for (pos = 0; pos < argc - 1; pos++) {
      if (strcmp(argv[pos], "--boomray") == 0) {
        REF_DBL xyz0[3], xyz1[3];

        char *boomray_filename;
        const char *vars[] = {"dp/pinf"};
        RAS(pos < argc - 7,
            "not enough arguments for --boomray <x0> <y0> <z0> <x1> <y1> <z1> "
            "<ray.tec>");
        boom_ray = REF_TRUE;
        if (NULL == dp_pinf) {
          REF_INT node;
          ref_malloc(dp_pinf, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
          each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
            REF_INT pressure_index = 4;
            REF_DBL gamma = 1.4;
            dp_pinf[node] =
                (field[pressure_index + ldim * node] - 1.0 / gamma) * gamma;
          }
        }
        xyz0[0] = atof(argv[pos + 1]);
        xyz0[1] = atof(argv[pos + 2]);
        xyz0[2] = atof(argv[pos + 3]);
        xyz1[0] = atof(argv[pos + 4]);
        xyz1[1] = atof(argv[pos + 5]);
        xyz1[2] = atof(argv[pos + 6]);
        boomray_filename = argv[pos + 7];
        RSS(ref_iso_boomray(boomray_filename, ref_grid, dp_pinf, 1, vars, xyz0,
                            xyz1),
            "boomray");
      }
    }
    if (boom_ray) {
      ref_free(dp_pinf);
      ref_free(field);
      RSS(ref_grid_free(ref_grid), "free grid");
      return REF_SUCCESS;
    }
  }

  RXS(ref_args_find(argc, argv, "--subtract", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    char *in_diff;
    REF_INT diff_ldim;
    REF_DBL *diff_field;
    REF_INT node, i;
    in_diff = argv[pos + 1];
    if (ref_mpi_once(ref_mpi)) printf("read diff solution %s\n", in_diff);
    RSS(ref_part_scalar(ref_grid, &diff_ldim, &diff_field, in_diff), "diff");
    ref_mpi_stopwatch_stop(ref_mpi, "read diff solution");
    REIS(ldim, diff_ldim, "difference field must have same leading dimension");
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      for (i = 0; i < ldim; i++) {
        field[i + ldim * node] -= diff_field[i + ldim * node];
      }
    }
    ref_free(diff_field);
    ref_mpi_stopwatch_stop(ref_grid_mpi(ref_grid), "diff field");
    for (i = 0; i < ldim; i++) {
      REF_DBL max_diff = 0.0;
      REF_DBL total_diff = 0.0;
      each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
        max_diff = MAX(max_diff, ABS(field[i + ldim * node]));
      }
      RSS(ref_mpi_max(ref_mpi, &max_diff, &total_diff, REF_DBL_TYPE),
          "mpi max");
      if (ref_mpi_once(ref_mpi)) printf("%d max diff %e\n", i, max_diff);
    }
  }

  RXS(ref_args_find(argc, argv, "--overfun", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_DBL *overflow;
    REF_INT node, i, ldim_overflow;
    ldim_overflow = ldim;
    ref_malloc(overflow, ldim_overflow * ref_node_max(ref_grid_node(ref_grid)),
               REF_DBL);
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      for (i = 0; i < ldim_overflow; i++) {
        overflow[i + ldim_overflow * node] = field[i + ldim_overflow * node];
      }
    }
    ldim = ldim_overflow - 1;
    ref_free(field);
    ref_malloc(field, (ldim)*ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
      REF_DBL rho, u, v, w, e_0, gamma, e_i, p;

      rho = overflow[0 + ldim_overflow * node];
      u = overflow[1 + ldim_overflow * node] / rho;
      v = overflow[2 + ldim_overflow * node] / rho;
      w = overflow[3 + ldim_overflow * node] / rho;
      e_0 = overflow[4 + ldim_overflow * node] / rho;
      gamma = overflow[5 + ldim_overflow * node];
      e_i = e_0 - 0.5 * (u * u + v * v + w * w);
      p = (gamma - 1.0) * rho * e_i;

      field[0 + ldim * node] = rho;
      field[1 + ldim * node] = u;
      field[2 + ldim * node] = v;
      field[3 + ldim * node] = w;
      field[4 + ldim * node] = p;

      for (i = 5; i < ldim; i++) {
        field[i + ldim * node] = overflow[(i + 1) + ldim_overflow * node];
      }
    }
    ref_free(overflow);
  }

  RXS(ref_args_find(argc, argv, "--fun-coffe", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_DBL *coffe;
    REF_INT variables;
    REF_INT node, i;
    variables = ldim;
    ldim *= 2;
    if (ref_mpi_once(ref_mpi))
      printf("creating steps: %d variables %d ldim\n", variables, ldim);
    ref_malloc(coffe, (ldim)*ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
    if (ref_grid_twod(ref_grid)) {
      THROW("2D translation not implemented");
    } else {
      each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
        REF_DBL gamma = 1.4;
        REF_DBL rho, u, w, temp, pressure;
        for (i = 0; i < variables; i++) {
          coffe[i + ldim * node] = field[i + variables * node];
        }
        rho = field[0 + variables * node];
        u = field[1 + variables * node];
        w = field[3 + variables * node];
        pressure = field[4 + variables * node];
        temp = gamma * (pressure / rho);
        coffe[1 + ldim * node] = -u;
        coffe[3 + ldim * node] = -w;
        coffe[4 + ldim * node] = temp;
        for (i = 0; i < variables; i++) {
          coffe[i + variables + ldim * node] = coffe[i + ldim * node];
        }
      }
    }
    ref_free(field);
    field = coffe;
  }

  for (pos = 0; pos < argc - 1; pos++) {
    if (strcmp(argv[pos], "--iso") == 0) {
      REF_DBL *scalar;
      REF_DBL threshold;
      char *out_iso;
      REF_GRID iso_grid;
      REF_INT var;
      REF_INT node;
      REF_DBL *out = NULL;
      RAS(pos < argc - 3,
          "not enough arguments for --iso <index> <threshold> <iso.extension>");
      var = atoi(argv[pos + 1]);
      threshold = atof(argv[pos + 2]);
      out_iso = argv[pos + 3];
      if (ref_mpi_once(ref_mpi))
        printf(" --iso %d %.4e %s\n", var, threshold, out_iso);
      ref_malloc(scalar, ref_node_max(ref_grid_node(ref_grid)), REF_DBL);
      each_ref_node_valid_node(ref_grid_node(ref_grid), node) {
        scalar[node] = field[var + ldim * node] - threshold;
      }
      RSS(ref_iso_insert(&iso_grid, ref_grid, scalar, ldim, field, &out),
          "iso");
      ref_mpi_stopwatch_stop(ref_mpi, "insert iso");
      if (ref_mpi_once(ref_mpi))
        printf("write isosurface %d ldim %s\n", ldim, out_iso);
      RSS(ref_gather_scalar_by_extension(iso_grid, ldim, out, NULL, out_iso),
          "gather");
      ref_mpi_stopwatch_stop(ref_mpi, "write isosurface geometry");

      ref_free(out);
      ref_grid_free(iso_grid);
      ref_free(scalar);
    }
  }

  for (pos = 0; pos < argc - 1; pos++) {
    if (strcmp(argv[pos], "--slice") == 0) {
      REF_DBL normal[3], offset;
      char *out_slice;
      REF_GRID slice_grid;
      REF_DBL *out = NULL;
      RAS(pos < argc - 5,
          "not enough arguments for --slice nx ny nz offset slice.extension");
      normal[0] = atof(argv[pos + 1]);
      normal[1] = atof(argv[pos + 2]);
      normal[2] = atof(argv[pos + 3]);
      offset = atof(argv[pos + 4]);
      out_slice = argv[pos + 5];
      if (ref_mpi_once(ref_mpi))
        printf(" --slice %6.3f %6.3f %6.3f %.4e %s\n", normal[0], normal[1],
               normal[2], offset, out_slice);
      RSS(ref_iso_slice(&slice_grid, ref_grid, normal, offset, ldim, field,
                        &out),
          "slice");
      ref_mpi_stopwatch_stop(ref_mpi, "insert slice");
      if (ref_mpi_once(ref_mpi))
        printf("write slice %d ldim %s\n", ldim, out_slice);
      RSS(ref_gather_scalar_by_extension(slice_grid, ldim, out, NULL,
                                         out_slice),
          "gather");
      ref_mpi_stopwatch_stop(ref_mpi, "write slice");
      ref_free(out);
      ref_grid_free(slice_grid);
    }
  }

  RXS(ref_args_find(argc, argv, "--surface", &pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY != pos) {
    REF_INT group;
    REF_CELL ref_cell;
    if (ref_mpi_once(ref_mpi)) printf("  --surface deleting 3D cells\n");
    surface_only = REF_TRUE;
    each_ref_grid_3d_ref_cell(ref_grid, group, ref_cell) {
      RSS(ref_cell_free(ref_cell), "free cell");
      RSS(ref_cell_create(&ref_grid_cell(ref_grid, group),
                          (REF_CELL_TYPE)group),
          "empty cell create");
      ref_cell = ref_grid_cell(ref_grid, group);
    }
  }

  if (strcmp(out_sol, "none") == 0) {
    if (ref_mpi_once(ref_mpi))
      printf("skipping write of %d ldim to %s\n", ldim, out_sol);
  } else {
    if (surface_only) {
      if (ref_mpi_once(ref_mpi))
        printf("write %d ldim solution surface to %s\n", ldim, out_sol);
    } else {
      if (ref_mpi_once(ref_mpi))
        printf("write %d ldim solution of " REF_GLOB_FMT " nodes to %s\n", ldim,
               ref_node_n_global(ref_grid_node(ref_grid)), out_sol);
    }
    RSS(ref_gather_scalar_by_extension(ref_grid, ldim, field, NULL, out_sol),
        "gather");
    ref_mpi_stopwatch_stop(ref_mpi, "write solution");
  }
  ref_free(field);
  RSS(ref_grid_free(ref_grid), "free grid");

  return REF_SUCCESS;
shutdown:
  if (ref_mpi_once(ref_mpi)) visualize_help(argv[0]);
  return REF_FAILURE;
}

static void echo_argv(int argc, char *argv[]) {
  int pos;
  printf("\n");
  for (pos = 0; pos < argc; pos++) printf(" %s", argv[pos]);
  printf("\n\n");
}

int main(int argc, char *argv[]) {
  REF_MPI ref_mpi;
  REF_INT help_pos = REF_EMPTY;
  REF_INT pos;

  RSS(ref_mpi_start(argc, argv), "start");
  RSS(ref_mpi_create(&ref_mpi), "make mpi");
  ref_mpi_stopwatch_start(ref_mpi);

  if (ref_mpi_once(ref_mpi)) {
    printf("refine %s on %d ranks\n", VERSION, ref_mpi_n(ref_mpi));
    echo_argv(argc, argv);
  }

  RXS(ref_args_find(argc, argv, "--help", &help_pos), REF_NOT_FOUND,
      "arg search");
  if (REF_EMPTY == help_pos) {
    RXS(ref_args_find(argc, argv, "-h", &help_pos), REF_NOT_FOUND,
        "arg search");
  }

  if (1 == argc || 1 == help_pos) {
    if (ref_mpi_once(ref_mpi)) {
      char egads_deps[1024];
      char migrate_deps[1024];
      RSS(ref_egads_list_dependencies(egads_deps), "egads deps");
      RSS(ref_migrate_list_dependencies(migrate_deps), "migrate deps");
      usage(argv[0]);
      printf("\ngeometry dependencies:%s\n", egads_deps);
      printf("parallel dependencies:%s\n", migrate_deps);
    }
    goto shutdown;
  }

  RXS(ref_args_find(argc, argv, "--timing", &pos), REF_NOT_FOUND, "arg search");
  if (REF_EMPTY != pos && pos < argc - 1) {
    ref_mpi_timing(ref_mpi) = atoi(argv[pos + 1]);
    if (ref_mpi_once(ref_mpi)) printf("--timing %d\n", ref_mpi_timing(ref_mpi));
  }

  if (strncmp(argv[1], "a", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(adapt(ref_mpi, argc, argv), "adapt");
    } else {
      if (ref_mpi_once(ref_mpi)) adapt_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "b", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(bootstrap(ref_mpi, argc, argv), "bootstrap");
    } else {
      if (ref_mpi_once(ref_mpi)) bootstrap_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "c", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(collar(ref_mpi, argc, argv), "collar");
    } else {
      if (ref_mpi_once(ref_mpi)) collar_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "d", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(distance(ref_mpi, argc, argv), "distance");
    } else {
      if (ref_mpi_once(ref_mpi)) distance_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "e", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(examine(ref_mpi, argc, argv), "examine");
    } else {
      if (ref_mpi_once(ref_mpi)) examine_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "g", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(grow(ref_mpi, argc, argv), "grow");
    } else {
      if (ref_mpi_once(ref_mpi)) grow_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "i", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(interpolate(ref_mpi, argc, argv), "interpolate");
    } else {
      if (ref_mpi_once(ref_mpi)) interpolate_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "l", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSB(loop(ref_mpi, argc, argv), "loop", {
        if (ref_mpi_once(ref_mpi)) loop_help(argv[0]);
      });
    } else {
      if (ref_mpi_once(ref_mpi)) loop_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "m", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(multiscale(ref_mpi, argc, argv), "multiscale");
    } else {
      if (ref_mpi_once(ref_mpi)) multiscale_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "n", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(node(ref_mpi, argc, argv), "translate");
    } else {
      if (ref_mpi_once(ref_mpi)) node_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "q", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(quilt(ref_mpi, argc, argv), "quilt");
    } else {
      if (ref_mpi_once(ref_mpi)) quilt_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "s", 1) == 0) {
    if (ref_mpi_once(ref_mpi))
      printf("  surface      depreciated, use translate ... --surface.\n");
    goto shutdown;
  } else if (strncmp(argv[1], "t", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(translate(ref_mpi, argc, argv), "translate");
    } else {
      if (ref_mpi_once(ref_mpi)) translate_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "v", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(visualize(ref_mpi, argc, argv), "visualize");
    } else {
      if (ref_mpi_once(ref_mpi)) visualize_help(argv[0]);
      goto shutdown;
    }
  } else if (strncmp(argv[1], "w", 1) == 0) {
    if (REF_EMPTY == help_pos) {
      RSS(with2matrix(ref_mpi, argc, argv), "mintersect");
    } else {
      if (ref_mpi_once(ref_mpi)) with2matrix_help(argv[0]);
      goto shutdown;
    }
  } else {
    if (ref_mpi_once(ref_mpi)) usage(argv[0]);
    goto shutdown;
  }

  ref_mpi_stopwatch_stop(ref_mpi, "done.");
shutdown:
  RSS(ref_mpi_free(ref_mpi), "mpi free");
  RSS(ref_mpi_stop(), "stop");

  return 0;
}

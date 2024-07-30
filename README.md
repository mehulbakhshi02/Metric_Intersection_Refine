# Metric_Intersection_Refine

Changes have been made to the Refine metric-based mesh adaptation code, developed by NASA, to enable the metric intersection of the Spalding law-of-the-wall based metric with any metric derived from a scalar flow field. The repository contains modifications exclusively in the source file ref_subcommand.c. The major changes are detailed as follows:

**1. Addition of New Command:**
- **Location:** Line 345
- **Description:** A new command, 'with2matrix', has been introduced. This command performs the metric intersection between two input metrics using the ref_matrix_intersect function.

**2. Modifications to Spalding Metric Function:**
- **Location:** Line 397
- **Description:** The spalding_metric function has been modified to write out the metric in .solb format, based on the Spalding u+ formulation, which is created in the backend of the original code.

**3. Loop Adjustment:**
- **Location:** Line 1075
- **Description:** The spalding_metric function has been extracted from the loop that performs multiple iterations of adaptation near the boundary layer. This adjustment ensures consistency in the number of base meshes between the Spalding metric and the scalar metric.

## Compilation

```
Install Refine
cd src
replace original ref_subcommand.c file with the modified file given here
cd ../build
make
make install
```

## Initial Test Runs

Some initial test runs were performed to check the intersection.

2D NACA 0012:\
![image](https://github.com/user-attachments/assets/abee73b6-cd2e-40cc-80af-fb83b9827db6)\
Mesh based on mach number adaptation.\
![image](https://github.com/user-attachments/assets/af399d97-f8cd-4056-b52a-143a55f2e51b)\
Mesh based on boundary layer adaptation (spalding metric).\
![image](https://github.com/user-attachments/assets/bb710f4f-56c0-4b97-a54d-389c00f42b8f)\
Mesh based on intersected metric.

3D OneraM6 Wing:\
![image](https://github.com/user-attachments/assets/ad99130d-e8b6-422f-89f1-6bfa6ae0b51b)\
Mesh based on mach number adaptation.\
![image](https://github.com/user-attachments/assets/a32cb4c7-7f71-4489-8f6c-43647b9587c2)\
Mesh based on boundary layer adaptation (spalding metric).\
![image](https://github.com/user-attachments/assets/ec0798b3-bb24-4ece-bb18-30d58e6a3436)\
Mesh based on intersected metric.






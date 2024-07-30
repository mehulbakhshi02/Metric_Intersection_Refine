# Metric_Intersection_Refine

Changes were made in Refine (metric-based mesh adaptation code) developed by Nasa to perform metric intersection of spalding law-of-the-wall based metric and any metric based on scalar flow field. The repository contains only the source file ref_subcommand.c in which changes were made.\

Details of the major changes made:\
ref_subcommand.c:\
337: New command 'with2matrix' added. The command performs the metric intersection between 2 input metric using the ref_matrix_intersect function.\
414: Changes in the spalding_metric function were made to write out the metric in .solb format (based on spalding u+) which is created in backend in the original code.\
1133: Spalding metric function was taken out of the loop that performs multiple iterations of adpatation near the boundary layer. (This step was performed to match the number of base mesh between spalding and scalar metric)\

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

Some initial test runs were performed to check the intersection.\
2D NACA 0012:\
![image](https://github.com/user-attachments/assets/abee73b6-cd2e-40cc-80af-fb83b9827db6)\
Mesh based on mach number adaptation.\
![image](https://github.com/user-attachments/assets/af399d97-f8cd-4056-b52a-143a55f2e51b)\
Mesh based on boundary layer adaptation (spalding metric).\
![image](https://github.com/user-attachments/assets/bb710f4f-56c0-4b97-a54d-389c00f42b8f)\
Mesh based on intersected metric.\



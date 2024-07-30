# Metric_Intersection_Refine

Changes were made in Refine (metric-based mesh adaptation code) developed by Nasa to perform metric intersection of spalding law-of-the-wall based metric and any metric based on scalar flow field. The repository contains only the source files in which changes were made.

Details of the major changes made:\
ref_subcommand.c:<br>
337: New command 'with2matrix' added. The command performs the metric intersection between 2 input metric.<br>
337: 

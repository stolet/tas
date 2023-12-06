#!/usr/bin/gnuplot -persist
set terminal pdf font "Latin Modern Roman,16"
set output "plot.pdf"

set multiplot layout 2, 1

set palette defined (1 "#fef0d9", 2 "#fdcc8a", 3 "#fc8d59", 4 "#d7301f")

set ylabel "Budget"
set ytics ("0.25" 0, "0.375" 1, "0.5" 2, "0.625" 3, "0.75" 4 , "0.875" 5, "1" 6, "1.125" 7, "1.25" 8)
set autoscale yfix

set xlabel "Boost"
set xtics ("0.25" 0, "0.5" 1, "0.75" 2, "1" 3, "1.25" 4, "1.5" 5, "1.75" 6, "2" 7)
set autoscale xfix

set cblabel "Victim 99p Latency [µs]" rotate by 270 offset 1
set cbrange[50:180]
set cbtics ("50" 50, "115" 115, "180" 180)
plot 'lat_99p.dat' matrix with image pixels notitle

set cblabel "Aggressor Throughput [mRPS]" rotate by 270 offset 1
set cbrange[800000:1200000]
set cbtics ("0.8" 800000, "1" 1000000, "1.2" 1200000)
plot 'tp.dat' matrix with image pixels notitle

#!/usr/bin/gnuplot -persist
set terminal pdf font "Latin Modern Roman,16"
set output "plot.pdf"

# set multiplot layout 1, 2

set xlabel "Agressor Connections"
set ylabel "Victim 99p Latency [µs]"
set key left top
set logscale y 10

plot 'lat_99p.dat' using 1:2:6:xtic(1) with yerrorlines title 'tas' linetype 1 ps 1, \
     'lat_99p.dat' using 1:5:9:xtic(1) with yerrorlines title 'ovs-linux' linetype 4 ps 1, \
     'lat_99p.dat' using 1:4:8:xtic(1) with yerrorlines title 'ovs-tas' linetype 3 ps 1, \
     'lat_99p.dat' using 1:3:7:xtic(1) with yerrorlines title 'virtuoso' linetype 2 ps 1, \


# set xlabel "Agressor Message Size [Bytes]"
# set ylabel "Victim 99p Latency [µs]"
# set key left top
# set logscale y 10
# set logscale x 2

# plot '../perf_iso_latmsize/lat_99p.dat' using 1:2:6 with yerrorlines title 'tas' linetype 1 ps 1, \
#      '../perf_iso_latmsize/lat_99p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 4 ps 1, \
#      '../perf_iso_latmsize/lat_99p.dat' using 1:4:8 with yerrorlines title 'ovs-tas' linetype 3 ps 1, \
#      '../perf_iso_latmsize/lat_99p.dat' using 1:3:7 with yerrorlines title 'virtuoso' linetype 2 ps 1, \
# # margins: left,right,bottom,top
# # spacing: xspacing,yspacing
# set multiplot layout 2,2 \
#               margins 0.1,0.95,0.15,0.90 \
#               spacing 0.1,0.07
              
# set label 1 "Aggresor Client # Connections" at screen 0.5, 0.03 center font "Latin Modern Roman,8"
# set label 2 "Victim Client Latency (µs)" at screen 0.01, 0.5 rotate by 90 center font "Latin Modern Roman,8"

# set title offset 0,-0.6

# # Plot 50p latency
# unset xtics
# set yrange [0:]
# set title "50p Latency"
# set key right bottom
# plot 'lat_50p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_50p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_50p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_50p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

# # Plot 90p latency
# unset xtics
# set yrange [0:]
# set title "90p Latency"
# set key right bottom
# plot 'lat_90p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_90p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_90p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_90p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

# # Plot 99p latency
# set xtics
# set xtics font "Latin Modern Roman,4"
# set yrange [0:]
# set title "99p Latency"
# set key right bottom
# plot 'lat_99p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_99p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_99p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_99p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

# # Plot 99.9p latency
# set xtics
# set xtics font "Latin Modern Roman,4"
# set yrange [0:]
# set title "99.9p Latency"
# set key right bottom
# plot 'lat_99.9p.dat' using 1:2:4 with yerrorlines title 'bare-virtuoso' linetype 4 ps 0.3, \
#      'lat_99.9p.dat' using 1:3:5 with yerrorlines title 'virtuoso' linetype 5 ps 0.3, \
#      # 'lat_99.9p.dat' using 1:5:9 with yerrorlines title 'ovs-linux' linetype 5 ps 0.3, \
#      # 'lat_99.9p.dat' using 1:6: with yerrorlines title 'ovs-tas' linetype 6 ps 0.3 w lp, \

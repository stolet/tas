set terminal png size 800,500 enhanced font "Arial,20"
set output 'plot.png'

red = "#FF0000";blue = "#0000FF";
set xrange [0:5]
set yrange [0:15000000]
set style data histogram
set style histogram cluster gap 1
set style fill solid
set boxwidth 0.9
set xtics ("1" 1, "2" 2, "3" 3, "4" 4)
set ylabel "System throughput per core [mRps]"
set ytics ("0" 0, "4" 4000000, "8" 8000000, "12" 12000000, "16" 16000000)
set xlabel "# of VMs"
set grid ytics

plot "resoeff-bar-tp.dat" using 2:xtic(1) title "Virtuoso" linecolor rgb red,   \
     "resoeff-bar-tp.dat" using 3:xtic(1) title "OvS-TAS" linecolor rgb blue,   \
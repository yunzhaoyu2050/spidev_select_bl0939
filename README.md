# spidev_select_bl0939
Read bl0939 data through the application layer &lt;spidev>

build:

    e.g: /xxx/bin/mipsel-openwrt-linux-uclibc-gcc -O2 -Wall spidev_select_bl0939.c -o spidev_bl0939

usage:

    -D --device   device to use (default /dev/spidev32766.1)

    -t --time     timer timing value (default 20ms)

    -h            help

test:

    platform: mt7628 openwrt

    ```
    root@Mt7628:~# ./spidev_bl0939 
    [[745,690,1004,854,805,986,717,753,708,749],[776,924,756,918,827,1112,844,847,792,787],561]
    [[821,820,927,713,674,966,761,798,802,889],[807,924,987,988,762,944,755,750,840,797],561]
    [[855,769,738,692,855,970,1004,821,919,754],[818,743,979,1076,748,920,1078,795,803,897],564]
    ```

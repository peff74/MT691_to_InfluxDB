#  MT691_to_InfluxDB

An **ESP8266** program for obtaining data from a **MT691** and saving it to an **InfluxDB**.

#  Features

 - Reads the data from the smartmeter every second
 - Sends the last value to InfluxDB every minute
 - Buffers data if no WiFi is available (up to 800 records)
 - Webpage with Grafana plugin and ESP monitoring
 - Telnet for monitoring


![Screenshot](Screenshot_50.jpg)

[![Hits](https://hits.seeyoufarm.com/api/count/incr/badge.svg?url=https%3A%2F%2Fgithub.com%2Fpeff74%2FMT691_to_InfluxDB&count_bg=%2379C83D&title_bg=%23555555&icon=&icon_color=%23E7E7E7&title=hits&edge_flat=false)](https://hits.seeyoufarm.com)

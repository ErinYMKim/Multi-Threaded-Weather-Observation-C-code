# Weather Observation System

This repository contains software for a real-time weather station information system, written in C.


## Overview

This software provides a real-time weather station information system designed to fetch, update, and manage weather data from the Bureau of Meteorology (BOM). It utilizes a multi-threaded approach to ensure live updates of weather data and interactive user engagement without any significant delays. 


## Structure

The software architecture is built around two primary threads:

Thread #1 (Weather Update Task): This thread is dedicated to fetching all weather station information from the BOM and updating it periodically. It also responds immediately to fetch station weather information upon receiving a signal from Thread #2.

Thread #2 (User Interface Task): This thread hosts the user interface for interacting with the system. Functions include adding or removing weather stations from the monitoring list and printing the list of current weather stations along with their latest weather data.

## Usage

To interact with the system, launch the program, which activates both threads. It then presents a user menu with the following options:

Show available stations: Lists all stations available in the station_data.txt file.

Add [station]: Adds a new weather station to the monitoring list (if it exists in the station_data.txt file).

Remove [station]: Removes an existing station from the list.

Print: Displays current weather data for all monitored stations in the list.

Quit: Exits the application.

Additionally, users can simulate adding all stations from the station_data.txt file by selecting option 8 and can remove all stations by selecting option 9 (these options are undocumented features for testing).

## Requirements

Libraries: This program requires the cJSON library for JSON parsing and libcurl for making HTTP requests.

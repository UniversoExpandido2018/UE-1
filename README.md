# SWGEmu Core3 #

## What is SWGEmu?

Star Wars Galaxies was a massively multi-player online role playing game introduced by Sony Online Entertainment in the year 2003 and shut down in 2011.
It is this game the SWGEmu project focuses to recreate at a specific milestone referred to as Pre-CU, or Pre-Combat Upgrade. The Combat Upgrade was a set of game changes which radically changed the game-play, to the dislike of thousands of players. These changes led to the founding of this project, in an attempt to "recreate" the game as it was during the Pre-CU era.
At SWGEmu, Emulator refers to the software the SWGEmu team is building. This Emulator is meant to imitate Sony Online Entertainment's server-side software, which hosted the galaxies of Star Wars Galaxies during the Pre-CU era.

#### "Universo Expandido" es un servidor basado en el sistema Pre-CU dedicado a roleplayers de habla espaÑola. Por ese motivo se está llevando a cabo la traducción del juego.

## How to Build

### Dependencies

  * CMake 3.1.0 or higher
  * BerkeleyDB 5.3
  * MySQL Client and Server
  * OpenSSL libraries
  * pthreads
  * Lua 5.3 libraries
  * Zlib libraries
  * g++ 5.4+ or compatible
  * engine3
  * java jre 1.7+

### Build

  * Install dependencies (Debian 9+ or Ubuntu 16.04+)

        sudo apt install build-essential libmysqlclient-dev liblua5.3-dev libdb5.3-dev libssl-dev cmake git default-jre

  * Install dependencies (RHEL/CentOS 8+ or Fedora 28+)

        sudo dnf install automake cmake git gcc gcc-c++ java-1.8.0-openjdk-headless libatomic libdb-devel lua-devel make mariadb-devel openssl-devel

  * Clone core3 repository somewhere  (~/git)

        mkdir -p ~/git
        cd ~/git
        git clone http://review.swgemu.com/Core3
  * Build Core3 with 8 threads

        cd MMOCoreORB
        make -j8
  * Import sql database

        mysql -h<MYSQLHOST> -u<MYSQLUSER> -p<MYSQLPASSWORD> < sql/swgemu.sql

## How to Run

    cd ~/git/Core3/MMOCoreORB/bin
    ./core3

## License

    Copyright (C) 2019 SWGEmu

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by the Free Software Foundation,
    either version 3 of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License along with this program.
    If not, see <http://www.gnu.org/licenses/>.

For more information, see https://review.swgemu.com.

Special thanks to SWGEmu-Team & Mod The Galaxy Developers (2021).


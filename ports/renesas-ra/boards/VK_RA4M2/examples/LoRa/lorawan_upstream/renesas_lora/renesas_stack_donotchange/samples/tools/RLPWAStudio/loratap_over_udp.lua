-- Copyright (c) 2021 Renesas Electronics Corporation
-- This software is released under the MIT License.
-- http://opensource.org/licenses/mit-license.php
--
-- LoRaTap over UDP dissector for Renesas LPWA Studio

local loratap_dissector = Dissector.get("loratap")
DissectorTable.get("udp.port"):add(54321, loratap_dissector)

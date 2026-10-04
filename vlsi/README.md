# VLSI designs — export from Cadence (Claude cannot open binary Cadence libraries)
For each design folder (lna_wifi_2.4GHz, lna_rfid_13.56MHz, dac_4bit_r2r) add:
- schematic.pdf or .png  (Virtuoso schematic: File > Export Image, or Print > PDF)
- netlist.scs            (ADE: Simulation > Netlist > Create, copy the file)
- sim_*.png              (S-parameters / noise figure / gain / INL-DNL / transient plots)
- results.md             (table: gain, NF, S11, power, bandwidth / DAC resolution, INL, DNL, settling)
- layout.png + drc_lvs.txt if a layout exists; technology (e.g. 180 nm) and supply voltage.
Next planned: digital HMAC-SHA-256 + AES-128 coprocessor (Vivado + ModelSim) in vlsi/digital_security_coprocessor/,
analog low-power SAR ADC (Virtuoso) in vlsi/sar_adc/.

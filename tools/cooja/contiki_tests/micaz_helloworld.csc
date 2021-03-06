<?xml version="1.0" encoding="UTF-8"?>
<simconf>
  <simulation>
    <title>Hello World (MicaZ)</title>
    <randomseed>generated</randomseed>
    <motedelay_us>1000000</motedelay_us>
    <radiomedium>
      se.sics.cooja.radiomediums.UDGM
      <transmitting_range>50.0</transmitting_range>
      <interference_range>100.0</interference_range>
      <success_ratio_tx>1.0</success_ratio_tx>
      <success_ratio_rx>1.0</success_ratio_rx>
    </radiomedium>
    <events>
      <logoutput>40000</logoutput>
    </events>
    <motetype>
      se.sics.cooja.avrmote.MicaZMoteType
      <identifier>micaz1</identifier>
      <description>MicaZ Mote Type #micaz1</description>
      <source>[CONTIKI_DIR]/examples/hello-world/hello-world.c</source>
      <commands>make hello-world.elf TARGET=micaz</commands>
      <firmware>[CONTIKI_DIR]/examples/hello-world/hello-world.elf</firmware>
      <moteinterface>se.sics.cooja.interfaces.Position</moteinterface>
      <moteinterface>se.sics.cooja.avrmote.interfaces.MicaZID</moteinterface>
      <moteinterface>se.sics.cooja.avrmote.interfaces.MicaZLED</moteinterface>
      <moteinterface>se.sics.cooja.avrmote.interfaces.MicaZRadio</moteinterface>
      <moteinterface>se.sics.cooja.avrmote.interfaces.MicaClock</moteinterface>
      <moteinterface>se.sics.cooja.avrmote.interfaces.MicaSerial</moteinterface>
      <moteinterface>se.sics.cooja.interfaces.Mote2MoteRelations</moteinterface>
      <moteinterface>se.sics.cooja.interfaces.MoteAttributes</moteinterface>
    </motetype>
    <mote>
      <interface_config>
        se.sics.cooja.interfaces.Position
        <x>36.478849033811386</x>
        <y>97.17795415366507</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        se.sics.cooja.avrmote.interfaces.MicaZID
        <id>1</id>
      </interface_config>
      <motetype_identifier>micaz1</motetype_identifier>
    </mote>
  </simulation>
  <plugin>
    se.sics.cooja.plugins.SimControl
    <width>280</width>
    <z>2</z>
    <height>160</height>
    <location_x>17</location_x>
    <location_y>16</location_y>
  </plugin>
  <plugin>
    se.sics.cooja.plugins.LogListener
    <plugin_config>
      <filter />
    </plugin_config>
    <width>680</width>
    <z>1</z>
    <height>240</height>
    <location_x>20</location_x>
    <location_y>285</location_y>
  </plugin>
  <plugin>
    se.sics.cooja.plugins.ScriptRunner
    <plugin_config>
      <scriptfile>[CONFIG_DIR]/hello-world.js</scriptfile>
      <active>true</active>
    </plugin_config>
    <width>600</width>
    <z>0</z>
    <height>535</height>
    <location_x>403</location_x>
    <location_y>23</location_y>
  </plugin>
</simconf>


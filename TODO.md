Er is nu bluetooth integratie,

dat moet op de manier die audio-tools library voorschrijft

issue:
op dit moment werkt het bijna goed, maar ik denk dat de routing beter kan aangezien er tijdens het indrukken van een sample, terwijl BT afspeelt er kraakjes ontstaan.

BT werkt zelf nu (eindelijk) goed en zonder buffer underruns en overruns. Wat in chil is, want in principe zou er ook geen buffer nodig zijn.
Wel moet de audio natuurlijk ook naar de scope gestuurd worden dus misschien zorgt dat voor RAM usage

Het probleem is dus als ik een sample in druk, het klinkt bijna heel de tijd goed maar op de 2de helft van de sample, ontstaan er toch vaak kleine mutes van 1 sample lang.

Dat kan dus ook komen omdat RAM gewoon maxed out is, door de BT stream + SD card reads.

Het kan zijn dat er verbetering mogelijk is met efrficientere en lichtere routing.
De delay line is tijdens BT gebruik al uit , aangezien dat alle RAM opslokte.

Dit is wat er nu staat op init:
INIT] SD OK
[HEAP] Before audio init: 173060 free
[PSRAM] Size=0 Free=0
[INIT] Audio pipeline OK
Settings mode switch initialized to: OFF
[INIT] Player OK
[BT] Heap before A2DP start: 150504
[BT] Heap after A2DP start: 56184
[BT] A2DP sink started as 'BANKRAKAKA'
[HEAP] Delay: max=57ms, free=56184 before alloc
[HEAP] After delay alloc: 51084 free
Loaded zoom=0.200000 from settings
Loaded oneshot=1 from settings
Loaded delay_ms=57.00 from settings
Loaded delay_fb=0.80 from settings
Loaded fb_hp=800.00 from settings
Loaded fb_lp=6000.00 from settings
Loaded filter_q=0.50 from settings
Loaded debug=0 from settings
[UI] Splash done, releasing display mutex

Klein ideeetje voor als er uberhautp nog ruimte over zou zijn

Knoppen wizard:

als er in de txt bestand op de sd card geen knoppen configuratie staat kan er een wizard starten die vraagt om de knoppen 1-6 , del en filterswitch een voor een aan te raken.

Dit scheelt tijd met het instellen.
Nu heb ik er een aantal samplers ingesteld op deze manier in de config/pins.h:

// constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 7, 2,3, 6, 5, 4};
// constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 3, 2,4, 5, 6, 7};
constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 2, 3,4, 1, 0, 5};
//constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 5,6,1, 4, 3, 2};

als er een configuratie voor is dan pakt die die. zo niet: start de wizard

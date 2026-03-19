Knoppen wizard:

als er in de txt bestand op de sd card geen knoppen configuratie staat kan er een wizard starten die vraagt om de knoppen 1-6 , del en filterswitch een voor een aan te raken.

Dit scheelt tijd met het instellen.
Nu heb ik er een aantal samplers ingesteld op deze manier in de config/pins.h:

// constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 7, 2,3, 6, 5, 4};
// constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 3, 2,4, 5, 6, 7};
constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 2, 3,4, 1, 0, 5};
//constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 5,6,1, 4, 3, 2};

als er een configuratie voor is dan pakt die die. zo niet: start te wizard


UI hulp:
zet de filt/vol 
de filter value (0%-100%) links onder (afhankelijk van de layout van de knoppe)
gewoon klein: 
del on/off
flt/vol: x%

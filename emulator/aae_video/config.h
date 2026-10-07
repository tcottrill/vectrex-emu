#ifndef CONFIG_H
#define CONFIG_H

// -----------------------------------------------------------------------------
// [vectrex-port] Trimmed local copy of the AAE config.h for the vector-glow
// renderer shim. Only the fields the kept AAE renderer paths actually read are
// required, but the full settings struct is kept here for fidelity so the AAE
// sources compile unchanged. Default member initializers below carry the
// Vectrex-appropriate values described in the port spec, so the C++17
// `inline settings config;` instance comes up correctly with NO separate
// definition or runtime initialization step.
//
// Fields actually consumed by the kept renderer code:
//   artcrop, artwork, bezel, corner_strength, debug_profile_code,
//   fire_point_size, forcesync, gain, glow_filter, glow2_*, line_smoothing,
//   linewidth, overlay, system_rotation, vecglow, vectrail.
// -----------------------------------------------------------------------------

// Named struct (not an unnamed typedef) so we can use default member
// initializers below (MSVC C7626 forbids them on unnamed typedef'd classes).
struct settings {
	// --- Renderer-relevant fields (Vectrex defaults applied) ---
	int   widescreen        = 0;
	int   overlay           = 1;
	float linewidth         = 1.5f;
	float pointsize         = 1.5f;
	int   gain              = 0;
	int   fire_point_size   = 2;
	int   artwork           = 0;
	int   bezel             = 0;
	int   artcrop           = 0;
	int   vecglow           = 60;
	int   vectrail          = 0;   // phosphor trail OFF by default (host enables per-game)
	float prescale          = 1.0f;
	// [vectrex-port] 0, as in AAE. The host's FrameLimiter is the sole pacer;
	// 1 made init_gl re-enable vsync after the host had turned it off.
	int   forcesync         = 0;

	// --- Modern beam renderer + glow (AAE defaults; host loads from [video]) ---
	float line_smoothing    = 1.0f;    // beam edge feather, logical units
	float corner_strength   = 0.85f;   // join disc radius / beam half-width
	int   shots_textured    = 0;       // Vectrex has no textured shots
	int   glow_filter       = 1;       // 0 = classic accumulate blur, 1 = dual-filter pyramid (default)
	float glow2_gain        = 10.0f;   // pyramid: final output gain
	float glow2_spread      = 1.0f;    // pyramid: tap radius scale
	float glow2_tail        = 0.6f;    // pyramid: down-level re-injection weight
	float glow2_core        = 1.0f;    // pyramid: unblurred-source weight
	int   debug_profile_code= 0;
	char* raster_effect     = (char*)"";
	int   system_rotation   = 0;

	// --- Remaining AAE settings (unused by the renderer, kept for fidelity) ---
	int   drawzero          = 0;
	int   colordepth        = 0;
	int   screenw           = 0;
	int   screenh           = 0;
	int   windowed          = 0;
	int   language          = 0;
	int   translucent       = 0;
	float translevel        = 0.0f;
	int   lives             = 0;
	int   m_line            = 0;
	int   m_point           = 0;
	int   monitor           = 0;
	int   gamma             = 0;
	int   bright            = 0;
	int   contrast          = 0;
	int   explode_point_size= 0;
	int   shotsize          = 0;
	int   cocktail          = 0;
	int   mainvol           = 0;
	int   pokeyvol          = 0;
	int   burnin            = 0;
	int   vid_rotate        = 0;
	int   psnoise           = 0;
	int   hvnoise           = 0;
	int   pshiss            = 0;
	int   noisevol          = 0;
	int   snappng           = 0;
	char* aspect            = nullptr;
	int   anisfilter        = 0;
	int   priority          = 0;
	int   dblbuffer         = 0;
	int   showinfo          = 0;
	int   hack              = 0;
	int   cheat             = 0;
	int   debug             = 0;
	int   audio_force_resample = 0;
	int   kbleds            = 0;
	int   samplerate        = 0;
	int   useMMCSS          = 0;
	int   boostThread       = 0;
	int   setTimerRes       = 0;
	int   preventSleep      = 0;
	int   confirm_exit      = 0;
	int   flip_gui_controls = 0;
	int   starting_monitor  = 0;
};

// Requires C++17 (matches the AAE original). No separate definition needed.
inline settings config;

#endif // CONFIG_H

// EZQuest: stable C export so the XR input adapter can reach Cbuf_AddText
// without depending on a C++ mangled symbol.

#include "cmd.h"

#if defined( ANDROID )
extern "C" __attribute__( ( visibility( "default" ) ) )
void EZQuest_Cbuf_AddText( const char *text )
{
	if ( text && text[0] )
		Cbuf_AddText( text );
}
#endif

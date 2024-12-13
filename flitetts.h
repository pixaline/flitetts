#ifndef FLITE_TTS_H
#define FLITE_TTS_H


#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/dictionary.h"
#include "servers/audio/audio_stream.h"
#include "scene/resources/packed_scene.h"

struct cst_voice_struct;
typedef struct cst_voice_struct cst_voice;

class FliteTTS : public Node {
	GDCLASS(FliteTTS, Node);

	public:

		// EXPORT
		
		Map<String, cst_voice *> voices;
		
		bool load_voice(const String & p_id, const String &p_path);
		Ref<AudioStreamSample> generate_tts(const String & c_voice, const String & c_text);
		float get_tts_length(const String &c_voice, const String &c_text);
		

		FliteTTS();
		~FliteTTS();

	protected:
		static void _bind_methods();


};

#endif // FLITE_TTS_H

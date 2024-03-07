#include "flitetts.h"
#include "scene/resources/audio_stream_sample.h"

#include "flite/flite.h"
#include "flite_addons.h"

extern "C" {
	void usenglish_init(cst_voice* v);
	cst_lexicon* cmulex_init(void);
}

bool FliteTTS::load_voice(const String & p_id, const String &p_path) {
	/*
	Error err;
	FileAccessRef f = FileAccess::open(p_path, FileAccess::READ, &err);

	ERR_FAIL_COND_V(!f, 0);
	*/
	
	cst_voice * voice = flite_voice_load(p_path.ascii().get_data());
	if(voice != NULL) {
		voices[p_id] = voice;
	}
	return (voice != NULL);
}


Ref<AudioStreamSample> FliteTTS::generate_tts(const String & c_voice, const String & c_text) {
	if(!voices.has(c_voice)) {
		return NULL;
	}
	
	cst_voice * voice = voices.find(c_voice)->value();
	if(voice == NULL) {
		return NULL;
	}
	
	cst_wave * wave = flite_ssml_text_to_wave(c_text.ascii().get_data(), voice);
	if(wave == NULL) {
		return NULL;
	}
	
	PoolByteArray wave_bytes;
	for (int i = 0; i < wave->num_samples; i++) {
		wave_bytes.append(wave->samples[i] & 0xff);
		wave_bytes.append((wave->samples[i] >> 8) & 0xff);
	}
	PoolVector<uint8_t> wave_vector(wave_bytes);
	
	Ref<AudioStreamSample> sample;
	sample.instance();
	
	sample->set_data(wave_vector);
	sample->set_mix_rate(wave->sample_rate);
	sample->set_format(AudioStreamSample::FORMAT_16_BITS);
	sample->set_loop_mode(AudioStreamSample::LOOP_DISABLED);
	//sample->set_loop_begin(0);
	//sample->set_loop_end(0);
	
	return sample;
}

void FliteTTS::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_voice"), &FliteTTS::load_voice);
	ClassDB::bind_method(D_METHOD("generate_tts"), &FliteTTS::generate_tts);
}

FliteTTS::FliteTTS() {
	flite_init();
	flite_add_lang("eng", usenglish_init, cmulex_init);
	flite_add_lang("usenglish", usenglish_init, cmulex_init);
}

FliteTTS::~FliteTTS() {
}



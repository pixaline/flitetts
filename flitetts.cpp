#include "flitetts.h"
#include "scene/resources/audio_stream_sample.h"

#include "flite/flite.h"
#include "flite_addons.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static FILE *fmemopen(void *buf, size_t len, const char *mode) {
	// Write buffer to a Windows temp file, return a FILE* to it.
	// The file is deleted when closed via the DELETE_ON_CLOSE flag.
	char tmp_path[MAX_PATH];
	char tmp_dir[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp_dir);
	GetTempFileNameA(tmp_dir, "flt", 0, tmp_path);

	HANDLE h = CreateFileA(
			tmp_path,
			GENERIC_READ | GENERIC_WRITE,
			0, NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
			NULL
			);
	if (h == INVALID_HANDLE_VALUE)
		return NULL;

	DWORD written;
	WriteFile(h, buf, (DWORD)len, &written, NULL);
	SetFilePointer(h, 0, NULL, FILE_BEGIN);

	int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
	if (fd == -1) {
		CloseHandle(h);
		return NULL;
	}

	return _fdopen(fd, "rb");
	// Closing this FILE* closes the fd, which closes the HANDLE,
	// which deletes the file due to FILE_FLAG_DELETE_ON_CLOSE.
}
#endif

extern "C" {
	void usenglish_init(cst_voice *v);
	cst_lexicon *cmulex_init(void);
}

bool FliteTTS::load_voice(const String & p_id, const String &p_res_path) {
	FileAccess *f = FileAccess::open(p_res_path, FileAccess::READ);
	if (f == nullptr) {
		ERR_PRINT("FliteTTS: cannot open resource: " + p_res_path);
		return false;
	}

	size_t len = f->get_len();
	// Use a raw heap buffer — fmemopen needs a plain pointer
	uint8_t *buf = (uint8_t *)memalloc(len);
	f->get_buffer(buf, len);
	f->close();
	memdelete(f);

	FILE *fd = fmemopen(buf, len, "rb");
	if (fd == nullptr) {
		ERR_PRINT("FliteTTS: fmemopen failed for: " + p_res_path);
		memfree(buf);
		return false;
	}

	cst_voice *voice = flite_cg_load_voice_from_file(fd);
	fclose(fd);
	memfree(buf);  // safe — cst_cg_load_db copies everything it needs

	if (voice == nullptr) {
		ERR_PRINT("FliteTTS: failed to load voice from resource: " + p_res_path);
		return false;
	}

	voices[p_id] = voice;
	return true;
}

float FliteTTS::get_tts_length(const String &c_voice, const String &c_text) {
	if (!voices.has(c_voice)) {
		return 0.0;
	}

	cst_voice *voice = voices.find(c_voice)->value();
	if (voice == NULL) {
		return 0.0;
	}

	return flite_ssml_text_length(c_text.ascii().get_data(), voice);


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

	return sample;
}

int FliteTTS::set_lexicon_entries(const String &c_voice, const PoolStringArray &c_lex_entries) {
	if(!voices.has(c_voice)) {
		return 0;
	}

	cst_voice * voice = voices.find(c_voice)->value();
	if(voice == NULL) {
		return 0;
	}

	cst_lexicon *lex = val_lexicon(feat_val(voice->features, "lexicon"));
	if(lex == NULL) {
		return 0;
	}

	// Get existing (if any) lexical addenda
	const cst_val *lex_addenda = NULL;
	if (feat_present(voice->features, "lex_addenda")) {
		lex_addenda = feat_val(voice->features, "lex_addenda");
	}

	// Do what cst_lex_load_addenda does
	cst_val * final_addenda = NULL;
	for(int i = 0; i < c_lex_entries.size(); i++) {
		cst_val * new_entry = cst_lex_make_entry(lex, c_lex_entries[i].ascii().get_data());
		if(new_entry != NULL) {
			final_addenda = cons_val(new_entry, final_addenda);
		}
	}
	final_addenda = val_reverse(final_addenda);

	// Concat with the existing voice lexicon
	final_addenda = val_append(final_addenda, (cst_val *) lex_addenda);
	if (lex->lex_addenda) {
		delete_val(lex->lex_addenda);
	}

	// Assign the added one to the voice
	lex->lex_addenda = final_addenda;

	return 1;

}

void FliteTTS::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_voice"), &FliteTTS::load_voice);
	ClassDB::bind_method(D_METHOD("generate_tts"), &FliteTTS::generate_tts);
	ClassDB::bind_method(D_METHOD("get_tts_length"), &FliteTTS::get_tts_length);
	ClassDB::bind_method(D_METHOD("set_lexicon_entries"), &FliteTTS::set_lexicon_entries);
}

FliteTTS::FliteTTS() {
	flite_init();
	flite_add_lang("eng", usenglish_init, cmulex_init);
	flite_add_lang("usenglish", usenglish_init, cmulex_init);
}

FliteTTS::~FliteTTS() {
}



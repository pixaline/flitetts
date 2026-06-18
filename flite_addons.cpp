#include <flite.h>
#include <cst_tokenstream.h>

static const char * const ssml_singlecharsymbols_general = "<>&/\";";
static const char * const ssml_singlecharsymbols_inattr = "=>;/\"";


extern "C" {
    #include "cst_cg.h"
    #include "cst_cg_map.h"

    cst_lexicon *cg_init_lang_lex(cst_voice *vox,
                                  const cst_lang *lang_table,
                                  const char *language);
}

static const char *ts_get_quoted_remainder(cst_tokenstream *ts)
{
	const char *q;

	q = ts_get_quoted_token(ts,'"','\\');

	return q;
}
static cst_features *ssml_get_attributes(cst_tokenstream *ts)
{
	cst_features *a = new_features();
	const char* name, *val;
	const char *fnn,*vnn;
	int i=0;

	set_charclasses(ts,
			ts->p_whitespacesymbols,
			ssml_singlecharsymbols_inattr,
			ts->p_prepunctuationsymbols,
			ts->p_postpunctuationsymbols);

	name = ts_get(ts);
	while (!cst_streq(">",name))
	{
		/* I want names and values to be const */
		fnn = "_name0";
		vnn = "_val0";
		// Tags with more than one attribute need to have additional
		// attributes defined here.
		if (cst_streq("volume", name))
		{
			fnn = "_name1"; vnn = "_val1";
		}
		else if (cst_streq("pitch", name))
		{
			fnn = "_name2"; vnn = "_val2";
		}
		else if (cst_streq("range", name))
		{
			fnn = "_name3"; vnn = "_val3";
		}
		if (cst_streq(name,"/"))
			feat_set_string(a,"_type","startend");
		else
		{
			feat_set_string(a,"_type","start");
			feat_set_string(a,fnn,name);
			if (cst_streq("=",ts_get(ts)))
			{
				val = ts_get_quoted_remainder(ts);
				feat_set_string(a,vnn,val);
			}
		}
		if (ts_eof(ts))
		{
			delete_features(a);
			return 0;
		}
		name = ts_get(ts);
		i++;
	}

	set_charclasses(ts,
			ts->p_whitespacesymbols,
			ssml_singlecharsymbols_general,
			ts->p_prepunctuationsymbols,
			ts->p_postpunctuationsymbols);

	return a;
}

static cst_utterance *ssml_apply_tag(const char *tag,
		cst_features *attributes,
		cst_utterance *u,
		cst_features *word_feats,
		cst_features *feats)
{
	const char *wavefilename;
	const char *vname;
	cst_voice *nvoice;
	cst_item *t;
	cst_relation *r;
	float break_size;

	if (cst_streq("BREAK",tag))
	{
		if (u &&
				((r = utt_relation(u,"Token")) != NULL) &&
				((t = relation_tail(r)) != NULL))
		{
			item_set_string(t,"break","1");
			if (cst_streq("size",get_param_string(attributes,"_name0","")))
			{
				break_size=feat_float(attributes,"_val0");
				item_set_float(t,"break_size",break_size);
			}
		}
	}
	else if (cst_streq("PROSODY",tag))
	{
		if (cst_streq("start",feat_string(attributes,"_type")))
		{
			/* Note SSML doesn't do stretch it does reciprical of stretch */
			if (cst_streq("rate",get_param_string(attributes,"_name0","")))
				feat_set_float(word_feats,"local_duration_stretch",
						1.0/feat_float(attributes,"_val0"));
			// volume is stored in _name1
			if (cst_streq("volume",get_param_string(attributes,"_name1","")))
				feat_set_float(word_feats,"local_gain",
						feat_float(attributes,"_val1")/100.0);
			// pitch is stored in _name2
			if (cst_streq("pitch", get_param_string(attributes, "_name2", "")))
			{
				feat_set_float(word_feats, "local_f0_mean", feat_float(attributes, "_val2"));
			}
			// range is stored in _name3
			if (cst_streq("range", get_param_string(attributes, "_name3", "")))
			{
				feat_set_float(word_feats, "local_f0_range",
						// shift by + 1.0 to allow 0.0 to be passed.
						feat_float(attributes, "_val3") + 1.0);
			}
		}
		else if (cst_streq("end",feat_string(attributes,"_type")))
		{
			feat_remove(word_feats,"local_duration_stretch");
			feat_remove(word_feats,"local_gain");
			feat_remove(word_feats, "local_f0_mean");
			feat_remove(word_feats, "local_f0_range");
		}

	}
	else if (cst_streq("PHONEME",tag))
	{
		if (cst_streq("start",feat_string(attributes,"_type")))
		{
			if (cst_streq("ph",get_param_string(attributes,"_name0","")))
			{
				const char *ph;
				ph = feat_string(attributes,"_val0");
				feat_set_string(word_feats,"phones",ph);
			}
		}
		else if (cst_streq("end",feat_string(attributes,"_type")))
		{
			feat_remove(word_feats,"phones");
		}

	}
	else if (cst_streq("SUB",tag))
	{
		if (cst_streq("start",feat_string(attributes,"_type")))
		{
			if (cst_streq("alias",get_param_string(attributes,"_name0","")))
			{
				const char *alias;
				alias = feat_string(attributes,"_val0");
				feat_set_string(word_feats,"ssml_alias",alias);
			}
		}
		else if (cst_streq("end",feat_string(attributes,"_type")))
		{
			feat_remove(word_feats,"ssml_alias");
		}

	}

	/* do stuff */
	/* flag what to do mark or end */
	/*
	   ph set attributes silence all contained tokens
	   break add to previous token a break marker
	   audio silence all following tokens (utt break)
	   insert waveform

*/

	return u;
}

cst_wave* real_flite_ssml_text_to_wave(const char* text, cst_voice* voice, float * durdest)
{
	cst_tokenstream* ts;
	if ((ts = ts_open_string(text,
					get_param_string(voice->features, "text_whitespace", NULL),
					get_param_string(voice->features, "text_singlecharsymbols", NULL),
					get_param_string(voice->features, "text_prepunctuation", NULL),
					get_param_string(voice->features, "text_postpunctuation", NULL)))
			== NULL)
	{
		return NULL;
	}

	/* this is a very ugly function, that might be better written with gotos */
	/* this just doesn't seem to be properly functions -- perhaps a proper */
	/* consumer/producer threaded model might be better here -- but its */
	/* not clear.  there is so much have-to-be-done-now vs note-for-later */
	/* code, that the code is far from clear, and probably not right */
	cst_features *ssml_feats, *ssml_word_feats;
	cst_features *attributes;
	const char *token = "";
	char *tag=NULL;
	cst_utterance *utt;
	cst_relation *tokrel;
	int num_tokens;
	cst_breakfunc breakfunc = default_utt_break;
	cst_uttfunc utt_user_callback = 0;
	cst_item *t;
	cst_voice *current_voice;
	int ssml_eou = 0;

	cst_wave *wave = NULL;
	cst_wave *w;

	ssml_feats = new_features();
	feat_set(ssml_feats,"current_voice",userdata_val(voice));
	feat_set(ssml_feats,"default_voice",userdata_val(voice));
	ssml_word_feats = new_features();
	set_charclasses(ts,
			" \t\n\r",
			ssml_singlecharsymbols_general,
			get_param_string(voice->features,"text_prepunctuation",""),
			get_param_string(voice->features,"text_postpunctuation","")
		       );

	if (feat_present(voice->features,"utt_break"))
		breakfunc = val_breakfunc(feat_val(voice->features,"utt_break"));

	if (feat_present(voice->features,"utt_user_callback"))
		utt_user_callback = val_uttfunc(feat_val(voice->features,"utt_user_callback"));

	num_tokens = 0;
	utt = new_utterance();

	tokrel = utt_relation_create(utt, "Token");
	while (!ts_eof(ts) || num_tokens > 0)
	{
		current_voice =
			(cst_voice *)val_userdata(feat_val(ssml_feats,"current_voice"));
		//printf("awb_debug prewhile %d %s\n",ssml_eou,token);
		if (ssml_eou == 0)
			token = ts_get(ts);
		else
		{
			if (!cst_streq("<",token))
				token = ts_get(ts);
			ssml_eou = 0;
		}
		while ((cst_streq("<",token)) && (ssml_eou == 0))
		{   /* a tag -- look ahead and process it to find out how to advance */
			tag = cst_upcase(ts_get(ts));
			/* printf("awb_debug tag is %s\n",tag); */
			if (cst_streq("/",tag)) /* an end tag */
			{
				cst_free(tag); tag=NULL;
				tag = cst_upcase(ts_get(ts));
				attributes = ssml_get_attributes(ts);
				feat_set_string(attributes,"_type","end");
			}
			else
				attributes = ssml_get_attributes(ts);
			token = ts_get(ts);  /* skip ">" */
			if (ssml_apply_tag(tag,attributes,utt,ssml_word_feats,ssml_feats))
				ssml_eou = 0;
			else
				ssml_eou = 1;

			delete_features(attributes);
			cst_free(tag); tag=NULL;
		}

		if ((cst_strlen(token) == 0) ||
				(num_tokens > 500) ||  /* need an upper bound */
				(ssml_eou == 1) ||  /* ssml tag was utterance break */
				(relation_head(tokrel) &&
				 breakfunc(ts,token,tokrel)))
		{
			/* an end of utt, so synthesize it */
			if (utt_user_callback)
				utt = (utt_user_callback)(utt);

			if (utt)
			{
				utt = flite_do_synth(utt,current_voice,utt_synth_tokens);
				if (feat_present(utt->features,"Interrupted"))
				{
					delete_utterance(utt); utt = NULL;
					break;
				}
				w = utt_wave(utt);
				if (durdest != NULL) {
					*durdest += (float)w->num_samples / (float)w->sample_rate;
				} else {
					if (wave == NULL) {
						wave = copy_wave(w);
					} else {
						wave = concat_wave(wave, w);
					}
				}
				delete_utterance(utt); utt = NULL;
			}
			else
				break;

			if (ts_eof(ts)) break;

			utt = new_utterance();
			tokrel = utt_relation_create(utt, "Token");
			num_tokens = 0;
		}

		if (feat_present(ssml_word_feats,"ssml_play_audio"))
		{
			w = val_wave(feat_val(ssml_word_feats,"ssml_play_audio"));
			/* should create an utterances with the waveform in it */
			/* have to stream it if there is streaming */
			if (utt) delete_utterance(utt);

			utt = utt_synth_wave(copy_wave(w),current_voice);
			if (utt_user_callback)
				utt = (utt_user_callback)(utt);
			if (durdest != NULL) {
				*durdest += (float)w->num_samples / (float)w->sample_rate;
			} else {
				if (wave == NULL) {
					wave = copy_wave(w);
				} else {
					wave = concat_wave(wave, w);
				}
			}
			delete_utterance(utt); utt = NULL;

			utt = new_utterance();
			tokrel = utt_relation_create(utt, "Token");
			num_tokens = 0;

			feat_remove(ssml_word_feats,"ssml_play_audio");
		}
		else if (!cst_streq("<",token))
		{  /* wasn't an ssml tag */
			num_tokens++;

			t = relation_append(tokrel, NULL);
			item_set_string(t,"name",token);
			item_set_string(t,"whitespace",ts->whitespace);
			item_set_string(t,"prepunctuation",ts->prepunctuation);
			item_set_string(t,"punc",ts->postpunctuation);
			/* mark it at the beginning of the token */
			item_set_int(t,"file_pos",
					ts->file_pos-(1+ /* as we are already on the next char */
						cst_strlen(token)+
						cst_strlen(ts->prepunctuation)+
						cst_strlen(ts->postpunctuation)));
			item_set_int(t,"line_number",ts->line_number);
			feat_copy_into(ssml_word_feats,item_feats(t));
		}
	}

	delete_utterance(utt);
	delete_features(ssml_feats);
	delete_features(ssml_word_feats);
	return wave;
}

float flite_ssml_text_length(const char *text, cst_voice *voice) {
	float dest = 0.0;
	real_flite_ssml_text_to_wave(text, voice, &dest);
	return dest;
}
cst_wave *flite_ssml_text_to_wave(const char *text, cst_voice *voice) {
	return real_flite_ssml_text_to_wave(text, voice, NULL);
}

// Mirrors cst_cg_load_voice but accepts an already-open FILE* instead of a path.
// The caller owns the FILE* and must fclose it after this returns.
cst_voice *flite_cg_load_voice_from_file(FILE *fd)
{
	cst_voice *vox;
	cst_lexicon *lex = NULL;
	int end_of_features;
	const char *language;
	const char *xname;
	cst_val *secondary_langs;
	const cst_val *sc;
	cst_cg_db *cg_db;
	char *fname;
	char *fval;
	int byteswapped = 0;
	int r;

	r = cst_cg_read_header(fd);
	if (r == CST_CG_BYTESWAPPED_VOICE)
		byteswapped = 1;
	else if (r != 0)
	{
		cst_errmsg("flite_cg_load_voice_from_file: unexpected header\n");
		return NULL;
	}

	vox = new_voice();

	fname = NULL;
	end_of_features = 0;
	while (end_of_features == 0)
	{
		cst_read_voice_feature(fd, &fname, &fval, byteswapped);
		if (cst_streq(fname, "end_of_features"))
			end_of_features = 1;
		else
		{
			xname = feat_own_string(vox->features, fname);
			flite_feat_set_string(vox->features, xname, fval);
		}
		cst_free(fname);
		cst_free(fval);
	}

	cg_db = cst_cg_load_db(vox, fd, byteswapped);
	if (cg_db == NULL)
	{
		delete_voice(vox);
		return NULL;
	}

	language = flite_get_param_string(vox->features, "language", "");

	secondary_langs =
		val_readlist_string(flite_get_param_string(vox->features,
					"secondary_languages", ""));
	for (sc = secondary_langs; sc; sc = val_cdr(sc))
		cg_init_lang_lex(vox, flite_lang_list, val_string(val_car(sc)));
	delete_val(secondary_langs);

	lex = cg_init_lang_lex(vox, flite_lang_list, language);
	if (lex == NULL)
	{
		cst_cg_free_db(fd, cg_db);
		delete_voice(vox);
		cst_errmsg("flite_cg_load_voice_from_file: lang/lex %s not supported\n", language);
		return NULL;
	}

	vox->name = cg_db->name;
	flite_feat_set_string(vox->features, "name", cg_db->name);

	flite_feat_set(vox->features, "lexicon", lexicon_val(lex));
	flite_feat_set(vox->features, "postlex_func", uttfunc_val(lex->postlex));

	flite_feat_set_string(vox->features, "no_segment_duration_model", "1");
	flite_feat_set_string(vox->features, "no_f0_target_model", "1");

	flite_feat_set(vox->features, "wave_synth_func", uttfunc_val(&cg_synth));
	flite_feat_set(vox->features, "cg_db", cg_db_val(cg_db));
	flite_feat_set_int(vox->features, "sample_rate", cg_db->sample_rate);

	return vox;
}

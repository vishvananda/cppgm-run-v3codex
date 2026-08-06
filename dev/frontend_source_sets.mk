# Per-tool implementation source lists for the compiler.
#
# Add dev/src/foo.cpp to the tools that use it by adding `foo` below. For
# subdirectories, use the path without `.cpp`, such as `parser/foo`.

FRONTEND_SOURCE_SET_TARGETS := abimangle pptoken posttoken ctrlexpr macro preproc recog nsdecl nsinit cy86 cppgm++ lowiropt lowir2cy86 lowir2native

FRONTEND_OBJ_BASENAMES_abimangle := abi_mangle_parse abi_mangle
FRONTEND_OBJ_BASENAMES_pptoken := pp_tokenizer
FRONTEND_OBJ_BASENAMES_posttoken := pp_tokenizer post_tokenizer
FRONTEND_OBJ_BASENAMES_ctrlexpr := pp_tokenizer post_tokenizer control_expression
FRONTEND_OBJ_BASENAMES_macro := pp_tokenizer post_tokenizer control_expression macro_processor
FRONTEND_OBJ_BASENAMES_preproc := pp_tokenizer post_tokenizer control_expression macro_processor
FRONTEND_OBJ_BASENAMES_recog := pp_tokenizer post_tokenizer control_expression macro_processor pa6_recognizer
FRONTEND_OBJ_BASENAMES_nsdecl := pp_tokenizer post_tokenizer control_expression macro_processor pa7_semantic
FRONTEND_OBJ_BASENAMES_nsinit := pp_tokenizer post_tokenizer control_expression macro_processor pa8_model pa8_parser pa8_semantic
FRONTEND_OBJ_BASENAMES_cy86 := pp_tokenizer post_tokenizer control_expression macro_processor cy86_frontend cy86_backend cy86_program
FRONTEND_OBJ_BASENAMES_cppgm++ := pp_tokenizer post_tokenizer control_expression macro_processor frontend_intern pa10_syntax_model pa10_syntax pa11_model pa11_semantic pa12_semantic pa12_semantic_declarations pa12_semantic_tables
FRONTEND_OBJ_BASENAMES_lowiropt :=
FRONTEND_OBJ_BASENAMES_lowir2cy86 := lowir_parse lowir_cy86
FRONTEND_OBJ_BASENAMES_lowir2native :=

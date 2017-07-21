#ifndef FACTORY_DECODER_REPETITION_HPP
#define FACTORY_DECODER_REPETITION_HPP

#include <string>

#include "Module/Decoder/Decoder.hpp"
#include "Module/Decoder/SISO.hpp"

#include "../Decoder.hpp"

namespace aff3ct
{
namespace factory
{
struct Decoder_repetition : public Decoder
{
	struct parameters : Decoder::parameters
	{
		virtual ~parameters() {}

		bool buffered = true;
	};

	template <typename B = int, typename R = float>
	static module::Decoder<B,R>* build(const parameters &params);

	static void build_args(arg_map &req_args, arg_map &opt_args);
	static void store_args(const tools::Arguments_reader& ar, parameters &params);
	static void group_args(arg_grp& ar);

	static void header(params_list& head_dec, const parameters& params);
};
}
}

#endif /* FACTORY_DECODER_REPETITION_HPP */
#include "Codec_RS.hpp"

using namespace aff3ct;
using namespace aff3ct::factory;

const std::string aff3ct::factory::Codec_RS_name   = "Codec RS";
const std::string aff3ct::factory::Codec_RS_prefix = "cdc";

Codec_RS::parameters
::parameters(const std::string &prefix)
: Codec          ::parameters(Codec_RS_name, prefix),
  Codec_SIHO_HIHO::parameters(Codec_RS_name, prefix),
  enc(new Encoder_RS::parameters("enc")),
  dec(new Decoder_RS::parameters("dec"))
{
	Codec::parameters::enc = enc;
	Codec::parameters::dec = dec;
}

Codec_RS::parameters
::~parameters()
{
	if (enc != nullptr) { delete enc; enc = nullptr; }
	if (dec != nullptr) { delete dec; dec = nullptr; }

	Codec::parameters::enc = nullptr;
	Codec::parameters::dec = nullptr;
}

Codec_RS::parameters* Codec_RS::parameters
::clone() const
{
	auto clone = new Codec_RS::parameters(*this);

	if (enc != nullptr) { clone->enc = enc->clone(); }
	if (dec != nullptr) { clone->dec = dec->clone(); }

	clone->set_enc(clone->enc);
	clone->set_dec(clone->dec);

	return clone;
}

void Codec_RS::parameters
::get_description(tools::Argument_map_info &args) const
{
	Codec_SIHO_HIHO::parameters::get_description(args);

	enc->get_description(args);
	dec->get_description(args);

	auto pdec = dec->get_prefix();
	auto penc = enc->get_prefix();

	args.erase({pdec+"-cw-size",   "N"});
	args.erase({pdec+"-info-bits", "K"});
	args.erase({pdec+"-fra",       "F"});
	args.erase({pdec+"-no-sys"        });

	args.add_link({pdec+"-corr-pow", "T"}, {penc+"-info-bits", "K"});
}

void Codec_RS::parameters
::store(const tools::Argument_map_value &vals)
{
	Codec_SIHO_HIHO::parameters::store(vals);

	enc->store(vals);

	this->dec->K        = this->enc->K;
	this->dec->N_cw     = this->enc->N_cw;
	this->dec->n_frames = this->enc->n_frames;

	dec->store(vals);

	if(this->dec->K != this->enc->K) // when -T has been given but not -K
		this->enc->K = this->dec->K;

	this->K    = this->enc->K * this->dec->m;
	this->N_cw = this->enc->N_cw * this->dec->m;
	this->N    = this->enc->N_cw * this->dec->m;
}

void Codec_RS::parameters
::get_headers(std::map<std::string,header_list>& headers, const bool full) const
{
	Codec_SIHO_HIHO::parameters::get_headers(headers, full);

	auto p = this->get_prefix();

	headers[p].push_back(std::make_pair("Symbols Source size",   std::to_string(this->enc->K   )));
	headers[p].push_back(std::make_pair("Symbols Codeword size", std::to_string(this->enc->N_cw)));

	enc->get_headers(headers, full);
	dec->get_headers(headers, full);
}

template <typename B, typename Q>
module::Codec_RS<B,Q>* Codec_RS::parameters
::build(module::CRC<B> *crc) const
{
	return new module::Codec_RS<B,Q>(*enc, *dec);

	throw tools::cannot_allocate(__FILE__, __LINE__, __func__);
}

template <typename B, typename Q>
module::Codec_RS<B,Q>* Codec_RS
::build(const parameters &params, module::CRC<B> *crc)
{
	return params.template build<B,Q>(crc);
}

// ==================================================================================== explicit template instantiation
#include "Tools/types.h"
#ifdef MULTI_PREC
template aff3ct::module::Codec_RS<B_8 ,Q_8 >* aff3ct::factory::Codec_RS::parameters::build<B_8 ,Q_8 >(aff3ct::module::CRC<B_8 >*) const;
template aff3ct::module::Codec_RS<B_16,Q_16>* aff3ct::factory::Codec_RS::parameters::build<B_16,Q_16>(aff3ct::module::CRC<B_16>*) const;
template aff3ct::module::Codec_RS<B_32,Q_32>* aff3ct::factory::Codec_RS::parameters::build<B_32,Q_32>(aff3ct::module::CRC<B_32>*) const;
template aff3ct::module::Codec_RS<B_64,Q_64>* aff3ct::factory::Codec_RS::parameters::build<B_64,Q_64>(aff3ct::module::CRC<B_64>*) const;
template aff3ct::module::Codec_RS<B_8 ,Q_8 >* aff3ct::factory::Codec_RS::build<B_8 ,Q_8 >(const aff3ct::factory::Codec_RS::parameters&, aff3ct::module::CRC<B_8 >*);
template aff3ct::module::Codec_RS<B_16,Q_16>* aff3ct::factory::Codec_RS::build<B_16,Q_16>(const aff3ct::factory::Codec_RS::parameters&, aff3ct::module::CRC<B_16>*);
template aff3ct::module::Codec_RS<B_32,Q_32>* aff3ct::factory::Codec_RS::build<B_32,Q_32>(const aff3ct::factory::Codec_RS::parameters&, aff3ct::module::CRC<B_32>*);
template aff3ct::module::Codec_RS<B_64,Q_64>* aff3ct::factory::Codec_RS::build<B_64,Q_64>(const aff3ct::factory::Codec_RS::parameters&, aff3ct::module::CRC<B_64>*);
#else
template aff3ct::module::Codec_RS<B,Q>* aff3ct::factory::Codec_RS::parameters::build<B,Q>(aff3ct::module::CRC<B>*) const;
template aff3ct::module::Codec_RS<B,Q>* aff3ct::factory::Codec_RS::build<B,Q>(const aff3ct::factory::Codec_RS::parameters&, aff3ct::module::CRC<B>*);
#endif
// ==================================================================================== explicit template instantiation

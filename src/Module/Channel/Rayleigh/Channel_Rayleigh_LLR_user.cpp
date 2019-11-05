#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

#include "Tools/Noise/Noise.hpp"
#include "Tools/Algo/Draw_generator/Gaussian_noise_generator/Standard/Gaussian_noise_generator_std.hpp"
#include "Tools/Algo/Draw_generator/Gaussian_noise_generator/Fast/Gaussian_noise_generator_fast.hpp"
#ifdef AFF3CT_CHANNEL_GSL
#include "Tools/Algo/Draw_generator/Gaussian_noise_generator/GSL/Gaussian_noise_generator_GSL.hpp"
#endif
#ifdef AFF3CT_CHANNEL_MKL
#include "Tools/Algo/Draw_generator/Gaussian_noise_generator/MKL/Gaussian_noise_generator_MKL.hpp"
#endif
#include "Tools/Exception/exception.hpp"
#include "Tools/Algo/Draw_generator/Gaussian_noise_generator/Standard/Gaussian_noise_generator_std.hpp"
#include "Module/Channel/Rayleigh/Channel_Rayleigh_LLR_user.hpp"

using namespace aff3ct;
using namespace aff3ct::module;

template <typename R>
Channel_Rayleigh_LLR_user<R>
::Channel_Rayleigh_LLR_user(const int N,
                            const bool complex,
                            tools::Gaussian_gen<R> &gaussian_generator,
                            const std::string& gains_filename,
                            const int gain_occurrences,
                            const bool add_users,
                            const int n_frames)
: Channel<R>(N, n_frames),
  complex(complex),
  add_users(add_users),
  gains(N * n_frames),
  gaussian_generator(&gaussian_generator),
  is_autoalloc_gaussian_gen(false),
  gain_occur(gain_occurrences),
  current_gain_occur(0),
  gain_index(0)
{
	const std::string name = "Channel_Rayleigh_LLR_user";
	this->set_name(name);

	if (complex || add_users)
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "Arguments 'complex' and 'add_users' are not supported yet.");

	if (gain_occurrences <= 0)
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "Argument 'gain_occurrences' must be strictly positive.");

	read_gains(gains_filename);
}

template <typename R>
Channel_Rayleigh_LLR_user<R>
::Channel_Rayleigh_LLR_user(const int N,
                            const bool complex,
                            const std::string& gains_filename,
                            const tools::Gaussian_noise_generator_implem implem,
                            const int seed,
                            const int gain_occurrences,
                            const bool add_users,
                            const int n_frames)
: Channel<R>(N, n_frames),
  complex(complex),
  add_users(add_users),
  gains(N * n_frames),
  gaussian_generator(nullptr),
  is_autoalloc_gaussian_gen(true),
  gain_occur(gain_occurrences),
  current_gain_occur(0),
  gain_index(0)
{
	const std::string name = "Channel_Rayleigh_LLR_user";
	this->set_name(name);

	switch (implem)
	{
		case tools::Gaussian_noise_generator_implem::STD:
			this->gaussian_generator = new tools::Gaussian_noise_generator_std<R>(seed);
			break;
		case tools::Gaussian_noise_generator_implem::FAST:
			this->gaussian_generator = new tools::Gaussian_noise_generator_fast<R>(seed);
			break;
#ifdef AFF3CT_CHANNEL_GSL
		case tools::Gaussian_noise_generator_implem::GSL:
			this->gaussian_generator = new tools::Gaussian_noise_generator_GSL<R>(seed);
			break;
#endif
#ifdef AFF3CT_CHANNEL_MKL
		case tools::Gaussian_noise_generator_implem::MKL:
			this->gaussian_generator = new tools::Gaussian_noise_generator_MKL<R>(seed);
			break;
#endif
		default:
			std::stringstream message;
			message << "Unsupported 'implem' ('implem' = " << (int)implem << ").";
			throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	};

	if (complex || add_users)
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "Arguments 'complex' and 'add_users' are not supported yet.");

	if (gain_occurrences <= 0)
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "Argument 'gain_occurrences' must be strictly positive.");

	read_gains(gains_filename);
}

template <typename R>
Channel_Rayleigh_LLR_user<R>
::~Channel_Rayleigh_LLR_user()
{
	if (this->is_autoalloc_gaussian_gen)
		delete gaussian_generator;
}

template <typename R>
void Channel_Rayleigh_LLR_user<R>
::set_seed(const int seed)
{
	this->gaussian_generator->set_seed(seed);
}

template <typename R>
void Channel_Rayleigh_LLR_user<R>
::read_gains(const std::string& gains_filename)
{
	if (gains_filename.empty())
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "Argument 'gains_filename' should not be empty.");

	std::ifstream file(gains_filename);

	if (file.is_open())
	{
		R val;
		while (!file.eof() && !file.bad())
		{
			file >> val;
			gains_stock.push_back(val);
		}

		file.close();
	}
	else
	{
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "Can't open '" + gains_filename + "' file.");
	}

	if(gains_stock.empty())
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, "The file '" + gains_filename + "' is empty.");
}

template <typename R>
void Channel_Rayleigh_LLR_user<R>
::add_noise_wg(const R *X_N, R *H_N, R *Y_N, const int frame_id)
{
	this->check_noise();

	if (frame_id != -1)
	{
		std::stringstream message;
		message << "'frame_id' has to be equal to -1 ('frame_id' = " << frame_id << ").";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

	// get all the needed gains from the stock
	for (unsigned i = 0; i < gains.size(); ++i)
	{
		gains[i] = gains_stock[gain_index];

		current_gain_occur++;
		if(current_gain_occur >= gain_occur)
		{
			current_gain_occur = 0;
			gain_index++;

			if(gain_index >= gains_stock.size())
				gain_index = 0;
		}
	}

	// generate the noise
	gaussian_generator->generate(this->noised_data, (R)this->noise->get_value()); // trow if noise is not SIGMA type

	// use the noise and the gain to modify the signal
	for (auto i = 0; i < this->N * this->n_frames; i++)
	{
		H_N[i] = this->gains[i];

		Y_N[i] = X_N[i] * H_N[i] + this->noised_data[i];
	}
}

template<typename R>
void Channel_Rayleigh_LLR_user<R>
::check_noise()
{
	Channel<R>::check_noise();

	this->noise->is_of_type_throw(tools::Noise_type::SIGMA);
}

// ==================================================================================== explicit template instantiation
#include "Tools/types.h"
#ifdef AFF3CT_MULTI_PREC
template class aff3ct::module::Channel_Rayleigh_LLR_user<R_32>;
template class aff3ct::module::Channel_Rayleigh_LLR_user<R_64>;
#else
template class aff3ct::module::Channel_Rayleigh_LLR_user<R>;
#endif
// ==================================================================================== explicit template instantiation

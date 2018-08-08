#include <chrono>
#include <sstream>
#include <numeric>
#include <type_traits>

#include "Tools/Perf/Reorderer/Reorderer.hpp"
#include "Tools/Exception/exception.hpp"

#include "Decoder_BCH_fast.hpp"

using namespace aff3ct;
using namespace aff3ct::module;

template <typename B, typename R>
Decoder_BCH_fast<B, R>
::Decoder_BCH_fast(const int& K, const int& N, const tools::BCH_polynomial_generator<B> &GF_poly, const int n_frames)
: Decoder         (K, N,                  n_frames, mipp::N<B>()           ),
  Decoder_BCH<B,R>(K, N, GF_poly.get_t(), n_frames                         ),
  t2              (2 * this->t                                             ),
  Y_N_reorderered (this->N                                                 ),
  elp             (this->N_p2_1+2, mipp::vector<mipp::Reg<B>>(this->N_p2_1)),
  discrepancy     (this->N_p2_1+2                                          ),
  l               (this->N_p2_1+2                                          ),
  u_lu            (this->N_p2_1+2                                          ),
  s               (t2+1                                                    ),
  reg             (this->t +1                                              ),
  m               (GF_poly.get_m()                                         ),
  d               (GF_poly.get_d()                                         ),
  alpha_to        (GF_poly.get_alpha_to()                                  ),
  index_of        (GF_poly.get_index_of()                                  )
{
	const std::string name = "Decoder_BCH_fast";
	this->set_name(name);

	if ((this->N - this->K) != GF_poly.get_n_rdncy())
	{
		std::stringstream message;
		message << "'N - K' is different than 'GF_poly.get_n_rdncy()' ('K' = " << K << ", 'N' = " << N
		        << ", 'GF_poly.get_n_rdncy()' = " << GF_poly.get_n_rdncy() << ").";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}

	if (this->N_p2_1 >= std::numeric_limits<B>::max() && std::is_unsigned<B>::value)
	{
		std::stringstream message;
		message << "'N_p2_1' must be less than 'std::numeric_limits<B>::max()' and 'B' must be signed ('N_p2_1' = "
		        << this->N_p2_1 << ", 'std::numeric_limits<B>::max()' = " << std::numeric_limits<B>::max() << ").";
		throw tools::invalid_argument(__FILE__, __LINE__, __func__, message.str());
	}
}

template <typename B, class A = std::allocator<B>>
mipp::Reg<B> read_array(const std::vector<B,A>& array, const mipp::Reg<B>& r_idx)
{
	B idx[mipp::N<B>()], val[mipp::N<B>()];
	r_idx.store(idx);

	for (auto j = 0; j < mipp::N<B>(); j++)
		val[j] = array[idx[j]];

	return mipp::Reg<B>(val);
}

template <typename B, class A = std::allocator<B>>
void write_array(std::vector<mipp::Reg<B>,A>& array, const mipp::Reg<B>& r_idx, const mipp::Msk<mipp::N<B>()>& m_idx,
                 const mipp::Reg<B>& r_values)
{
	mipp::vector<B> v_iota(mipp::N<B>());
	std::iota(v_iota.begin(), v_iota.end(), 0);
	mipp::Reg<B> r_iota = v_iota.data();

	for (auto j = 0; j < mipp::N<B>(); j++)
	{
		auto idx = r_idx[j];
		array[idx] = mipp::blend(r_values, array[idx], m_idx & (r_iota == j));
	}
}

template <typename B>
mipp::Reg<B> operator%(const mipp::Reg<B>& r, int mod)
{
	return r & (B)mod; // works only with mod == 2^n -1
}

template <typename B, typename R>
void Decoder_BCH_fast<B, R>
::_decode(B *Y_N, const int frame_id)
{
	const auto r_zero = mipp::Reg<B>((B)0);
	const auto r_one  = mipp::Reg<B>((B)1);
	const auto r_mone = mipp::Reg<B>((B)-1);


	// reorder data into mipp registers
	std::vector<const B*> frames(mipp::N<B>());
	for (auto f = 0; f < mipp::N<B>(); f++) frames[f] = Y_N + f * this->N;
	tools::Reorderer_static<B,mipp::N<B>()>::apply(frames, (B*)this->Y_N_reorderered.data(), this->N);

	// form the syndromes
	auto syn_error = r_zero != r_zero; // init to false

	std::fill(s.begin(), s.end(), r_zero);

	for (auto i = 1; i <= t2; i++)
	{
		for (auto j = 0; j < this->N; j++)
		{
			const mipp::Reg<B> r_alpha = alpha_to[(i * j) % this->N_p2_1];
			s[i] ^= mipp::blend(r_alpha, r_zero, Y_N_reorderered[j] != r_zero);
		}

		syn_error |= s[i] != r_zero;

		s[i] = read_array(index_of, s[i]);
	}

	if (!mipp::testz(syn_error))
	{
		// initialise table entries
		discrepancy[0] = r_zero; // index form
		discrepancy[1] = s[1]; // index form
		elp[0][0] = r_zero; // index form
		elp[1][0] = r_one; // polynomial form
		for (auto i = 1; i < t2; i++)
		{
			elp[0][i] = r_mone; // index form
			elp[1][i] = r_zero; // polynomial form
		}
		l[0]    = r_zero;
		l[1]    = r_zero;
		u_lu[0] = r_mone;
		u_lu[1] = r_zero;

		auto u = 0;
		auto m_process = syn_error;

		do
		{
			u++;
			mipp::Reg<B> r_u = (B)u;
			const auto u_m1 = u - 1;
			const auto u_p1 = u + 1;

			const auto m_disc     = (discrepancy[u] == r_mone) & m_process;
			const auto m_not_disc = (discrepancy[u] != r_mone) & m_process;

			//***** part if (discrepancy[u] == -1)
			if (!mipp::testz(m_disc))
			{
				l[u_p1] = mipp::blend(l[u], l[u_p1], m_disc);
				B i = 0;
				auto m_lu = (l[u] >= i) & m_disc;
				while(!mipp::testz(m_lu))
				{
					elp[u_p1][i] = mipp::blend(elp[u][i], elp[u_p1][i], m_lu);
					elp[u   ][i] = mipp::blend(read_array(index_of, elp[u][i] % this->N_p2_1), elp[u][i], m_lu);

					i++;
					m_lu = (l[u] >= i) & m_disc;
				}
			}

			//**** part if (discrepancy[u] != -1)
			if (!mipp::testz(m_not_disc))
			{
				// search for words with greatest u_lu[q] for which d[q] != -1
				mipp::Reg<B> r_disc_q, r_l_q = l[u_m1], r_u_lu_q = u_lu[u_m1], r_q = u_m1;
				mipp::vector<mipp::Reg<B>> r_elp_q = elp[u_m1];

				for (auto q = u_m1 - 1; q >= 0; q--)
				{
					auto m_ok = m_not_disc & (discrepancy[q] != r_mone) & (r_u_lu_q > u_lu[q]);

					r_q      = mipp::blend(mipp::Reg<B>(q), r_q,      m_ok);
					r_disc_q = mipp::blend( discrepancy[q], r_disc_q, m_ok);
					r_l_q    = mipp::blend(           l[q], r_l_q,    m_ok);
					r_u_lu_q = mipp::blend(        u_lu[q], r_u_lu_q, m_ok);

					for (unsigned i = 0; i < r_elp_q.size(); i++)
						r_elp_q[i] = mipp::blend(elp[q][i], r_elp_q[i], m_ok);
				}

				// store degree of new elp polynomial
				auto l_u_q = r_l_q + r_u - r_q;
				l_u_q   = mipp::blend(l_u_q, l[u   ], l[u] <= l_u_q);
				l[u_p1] = mipp::blend(l_u_q, l[u_p1],    m_not_disc);

				// form new elp(x)
				for (auto i = 0; i < t2; i++)
					elp[u_p1][i] = mipp::blend(r_zero, elp[u_p1][i], m_not_disc);

				auto l_max = mipp::hmax(mipp::blend(r_l_q, r_zero, m_not_disc));
				for (auto i = 0; i <= l_max; i++)
				{
					auto m_ok = m_not_disc & (r_elp_q[i] != r_mone) & (r_l_q >= i);

					if (mipp::testz(m_ok))
						continue;

					const auto r_alpha_idx = (r_elp_q[i] - r_disc_q + discrepancy[u] + this->N_p2_1) % this->N_p2_1;
					const auto r_idx = mipp::Reg<B>(i) + r_u - r_q;
					write_array(elp[u_p1], r_idx, m_ok, read_array(alpha_to, r_alpha_idx));
				}

				l_max = mipp::hmax(mipp::blend(l[u], r_zero, m_not_disc));
				for (auto i = 0; i <= l_max; i++)
				{
					auto m_ok = m_not_disc & (l[u] >= i);
					elp[u_p1][i] = mipp::blend(elp[u][i] ^ elp[u_p1][i], elp[u_p1][i], m_ok);
					elp[u   ][i] = read_array(index_of, elp[u][i] % this->N_p2_1);
				}
			}

			u_lu[u_p1] = r_u - l[u_p1];

			// form (u_p1)th discrepancy
			if (u < t2)
			{
				// no discrepancy computed on last iteration
				const auto m_s         = s[u_p1] != -1;
				const auto r_alpha_idx = mipp::blend(s[u_p1], r_zero, m_s) % this->N_p2_1;
				const auto r_read      = mipp::blend(read_array(alpha_to, r_alpha_idx), r_zero, m_s);
				discrepancy[u_p1]      = mipp::blend(r_read, discrepancy[u_p1], m_process);


				auto l_max = mipp::hmax(mipp::blend(l[u_p1], r_zero, m_not_disc));
				for (auto i = 1; i <= l_max; i++)
				{
					auto u_p1_i = ((u_p1 - i) < 0) ? 0 : (u_p1 - i);
					auto m_ok = m_process & (s[u_p1_i] != r_mone) & (elp[u_p1][i] != r_zero) & (l[u_p1] >= i);

					if (mipp::testz(m_ok))
						continue;

					const auto r_alpha_idx = (s[u_p1_i] + read_array(index_of, elp[u_p1][i] % this->N_p2_1)) % this->N_p2_1;
					const auto r_alpha = read_array(alpha_to, r_alpha_idx);
					const auto r_disc  = discrepancy[u_p1] ^ r_alpha;

					discrepancy[u_p1] = mipp::blend(r_disc, discrepancy[u_p1], m_ok);
				}

				/* put d.at(u_p1) into index form */
				discrepancy[u_p1] = mipp::blend(read_array(index_of, discrepancy[u_p1] % this->N_p2_1), discrepancy[u_p1], m_process);
			}

			m_process &= l[u_p1] <= this->t;
		} while (u < t2 && !mipp::testz(m_process));


		u++;
		// Correct errors
		const auto m_corr = l[u] <= this->t;

		if (!mipp::testz(m_corr))
		{
			// Chien search: find roots of the error location polynomial
			auto l_max = mipp::hmax(mipp::blend(l[u], r_zero, m_corr));
			for (auto i = 1; i <= l_max; i++)
			{
				auto r_idx = mipp::blend(elp[u][i], r_zero, l[u] >= i) % this->N_p2_1;
				r_idx = (read_array(index_of, r_idx) + i) % this->N_p2_1; // min 0 even if index_of == -1
				reg[i] = read_array(alpha_to, r_idx);
			}

			mipp::Reg<B> count = r_zero;
			for (auto i = 1; i <= this->N_p2_1; i++)
			{
				mipp::Reg<B> q = r_one;
				for (auto j = 1; j <= l_max; j++)
					q = mipp::blend(q ^ reg[j], r_one, l[u] >= i);

				auto m_flip = (q == r_zero) & m_corr;
				count += mipp::blend(r_one, r_zero, m_flip);

				auto idx = this->N_p2_1 - i;
				Y_N_reorderered[idx] = mipp::blend(Y_N_reorderered[idx] ^ r_one, Y_N_reorderered[idx], m_flip);
			}

			syn_error &= count != l[u];
		}
	}

	mipp::toReg<B>(~syn_error).store(this->last_is_codeword.data() + frame_id);


	// reorder data into standard registers
	std::vector<B*> frames_rev(mipp::N<B>());
	for (auto f = 0; f < mipp::N<B>(); f++) frames_rev[f] = Y_N + f * this->N;
	tools::Reorderer_static<B,mipp::N<B>()>::apply_rev((B*)this->Y_N_reorderered.data(), frames_rev, this->N);
}

// ==================================================================================== explicit template instantiation
#include "Tools/types.h"
#ifdef MULTI_PREC
template class aff3ct::module::Decoder_BCH_fast<B_8,Q_8>;
template class aff3ct::module::Decoder_BCH_fast<B_16,Q_16>;
template class aff3ct::module::Decoder_BCH_fast<B_32,Q_32>;
template class aff3ct::module::Decoder_BCH_fast<B_64,Q_64>;
#else
template class aff3ct::module::Decoder_BCH_fast<B,Q>;
#endif
// ==================================================================================== explicit template instantiation
#include "SC_PlugIn.hpp"
#include "ElementaryCA.hpp"
#include "BoidFlock.hpp"
#include "Boid.hpp"
#include "Pvector.hpp"
#include <algorithm>

using Automata = CA<Iteration_Vector>;

World* g_pWorld = nullptr;

// InterfaceTable contains pointers to functions in the host (server).
static InterfaceTable* ft;

// A struct to hold data used to calculate each generated sine wave value
struct SinWave {
	int32 m_phase;
	int32 phase;
	int32 i_freq;
	int32 phaseinc;
	float sin_val;
	float amp;
};

struct EmergentUGen : public SCUnit {
public:
	EmergentUGen(int seed = 0, int num_waves = 1) : m_seed{ seed }, m_num_waves{ num_waves } {
		// Initialise a pointer to the random number buffer
		rand_buf = ctor_get_buf(in0(0));
		// Initialise global reference to world
		g_pWorld = world;

		// Initialise variables for sine wave oscillators
		int tableSize2 = ft->mSineSize;
		m_radtoinc = tableSize2 * rtwopi * 65536.0;
		m_cpstoinc = tableSize2 * SAMPLEDUR * 65536.0;
		m_lomask = (tableSize2 - 1) << 3;
		m_phasein = 0.0;
		waves = (SinWave*)RTAlloc(world, m_num_waves * sizeof(SinWave));
		for (int i = 0; i < m_num_waves; i++) {
			waves[i].m_phase = (int32)(m_phasein * m_radtoinc);
		}

		if (waves == NULL) {
			set_calc_function<EmergentUGen, &EmergentUGen::clear>();

			if (world->mVerbosity > -2) {
				Print("Failed to allocate memory for ugen.\n");
			}
			return;
		}
	}

	~EmergentUGen() {
		RTFree(world, waves);
	}

protected:
	const SndBuf* rand_buf;
	const int m_seed;
	const int m_num_waves;
	float m_phasein;
	double m_radtoinc;
	double m_cpstoinc;
	int32 m_lomask;
	SinWave* waves;
	const Unit* unit = this;
	World* world = unit->mWorld;

	// Initialise a pointer to a buffer using a buffer number sent from the SC client 
	SndBuf* ctor_get_buf(float index) {
		float fbufnum = index;
		fbufnum = sc_max(0.f, fbufnum);
		uint32 bufnum = (int)fbufnum;
		SndBuf* buf;
		if (bufnum >= world->mNumSndBufs) {
			int localBufNum = bufnum - world->mNumSndBufs;
			Graph* parent = unit->mParent;
			if (localBufNum <= parent->localBufNum) {
				buf = parent->mLocalSndBufs + localBufNum;
			}
			else {
				bufnum = 0;
				buf = world->mSndBufs + bufnum;
			}
		}
		else {
			buf = world->mSndBufs + bufnum;
		}
		return buf;
	};


	void clear(int inNumSamples) {
		ClearUnitOutputs(this, inNumSamples);
	}

	template <typename UnitType, void (UnitType::* PointerToMember)(int)>
	void set_calc_function(void) {
		mCalcFunc = make_calc_function<UnitType, PointerToMember>();
		clear(1);
	}

	float buf_rand(int idx) {
		return rand_buf->data[(idx + m_seed) % rand_buf->samples];
	}
};

struct ElementaryCA : public EmergentUGen {
public:
	ElementaryCA() : EmergentUGen((int)in0(6), (int)in0(3) * (int)in0(4)) {
		// Initialise a list of flags to enable various frequency partials in the final output
		//seq_buf = ctor_get_buf(in0(5));
		if (!m_random) {
			binary_from_integer();
		}
		runAutomaton();
		set_calc_function<ElementaryCA, &ElementaryCA::next_a>();

		next_a(1);
	}

	~ElementaryCA() {
		RTFree(world, sarray);
	}

private:
	const int m_wolfram_code = (int)in0(2);
	const int m_num_columns = (int)in0(3);
	const int m_num_rows = (int)in0(4);

	float m_width = in0(7);
	float m_odd_skew = in0(8);
	float m_even_skew = in0(9);
	float m_amp_tilt = in0(10);
	float m_balance = in0(11);
	const bool m_random = ((int)in0(12)) > 0;

	//SndBuf* seq_buf;

	int* sarray = (int*)RTAlloc(world, m_num_columns * m_num_rows * sizeof(int));
	Automata automata;

	void binary_from_integer() {
		int i = 0;
		int seq = (int)in0(5);
		while (seq >= 0 && i < m_num_columns) {

			// storing remainder in binary array 
			sarray[m_num_columns - 1 - i] = seq % 2;
			seq = seq / 2;
			i++;
		}
	}

	void runAutomaton() {
		Iteration_Vector initial_state(m_num_columns);
		if (m_random) {
			for (int i = 0; i < m_num_columns; i++) {
				int r = (buf_rand(i) <= 0.5 ? 0 : 1);
				initial_state.set(i, r);
			}
		}
		else
		{
			for (int i = 0; i < m_num_columns; i++) {
				//initial_state.set(i, (int)seq_buf->data[i]);
				initial_state.set(i, sarray[i]);

			}
		}
		automata.init(m_wolfram_code, m_num_columns, m_num_rows, initial_state);
		automata.evolve();
	}

	void next_a(int inNumSamples) {
		float* table0 = ft->mSineWavetable;
		float* table1 = table0 + 1;
		float* outBuf = out(0);
		float freqin = in0(1);
		// get phase from struct and store it in a local variable.
		// The optimizer will cause it to be loaded it into a register.
		float phasein = 0.0;

		int max_partials = (int)(((0.48 * world->mSampleRate) - freqin) / freqin);
		int active_waves = std::min(m_num_waves, max_partials);

		for (int i = 0; i < active_waves; i++) {
			waves[i].phase = waves[i].m_phase;
		}
		int32 lomask = m_lomask;

		//automata.store(seq_buf, active_waves);

		automata.store(sarray, active_waves);

		for (int i = 0; i < inNumSamples; ++i) {
			float out_val = 0.0;
			for (int j = 0; j < active_waves; j++) {
				if (i == 0) {
					waves[j].amp = 0.0;
					//if (j == 0) {
					//	waves[j].amp = 1.0;
					//}
					//else {
						waves[j].amp = powf(j+1, -m_amp_tilt);// * powf(e.filter, i);
						if (j % 2 == 0) {
							if (m_balance > 0.0) {
								waves[j].amp *= 1.0 - m_balance;
							}
						}
						else {
							if (m_balance < 0.0) {
								waves[j].amp *= 1.0 + m_balance;
							}
						}
					//}

					waves[j].i_freq = (int32)(m_cpstoinc * setPartial(freqin, j));
					waves[j].phaseinc = waves[j].i_freq + (int32)(CALCSLOPE(phasein, m_phasein) * m_radtoinc);
					m_phasein = phasein;
				}
				waves[j].sin_val = lookupi1(table0, table1, waves[j].phase, lomask);
				//out_val += (waves[j].sin_val * waves[j].amp * seq_buf->data[j] * sc_reciprocal((float)active_waves));
				out_val += (waves[j].sin_val * waves[j].amp * sarray[j] * sc_reciprocal((float)active_waves));
				waves[j].phase += waves[j].phaseinc;
			}
			outBuf[i] = out_val;
		}

		for (int j = 0; j < active_waves; j++) {
			waves[j].m_phase = waves[j].phase;
		}
	}

	float setPartial(float freqin, int idx) {
		float ii = idx + 1;
		if (idx % 2 == 0) {
			ii += m_even_skew;
		}
		else {
			ii += m_odd_skew;
		}
		return freqin * powf(ii, m_width);
	}
};

struct Flock : public EmergentUGen {
public:
	Flock() : EmergentUGen((int)in0(3), (int)in0(4)) {
		for (int i = 0; i < m_num_waves; i++) {
			float vx = cos(buf_rand(i)) / 8.0;
			float vy = sin(buf_rand(i + 1)) / 8.0;
			Boid b((WIDTH / 2.0), (HEIGHT / 2.0), vx, vy);
			flock.addBoid(b);
		}
		set_calc_function<Flock, &Flock::next_a>();
		next_a(1);
	}

private:
	const float WIDTH = 1000.0;
	const float HEIGHT = 1000.0;
	BoidFlock flock;

	void next_a(int inNumSamples) {
		float* table0 = ft->mSineWavetable;
		float* table1 = table0 + 1;
		float* outBuf = out(0);
		float freqin = in0(1);
		// get phase from struct and store it in a local variable.
		// The optimizer will cause it to be loaded it into a register.
		float phasein = in0(2);

		for (int i = 0; i < m_num_waves; i++) {
			waves[i].phase = waves[i].m_phase;
		}
		int32 lomask = m_lomask;

		flock.flocking();

		for (int i = 0; i < inNumSamples; ++i) {
			float out_val = 0.0;
			for (int j = 0; j < m_num_waves; j++) {
				if (i == 0) {
					waves[j].i_freq = (int32)(m_cpstoinc * (freqin + (flock.getBoid(j).angle(flock.getBoid(j).velocity))));
					waves[j].phaseinc = waves[j].i_freq + (int32)(CALCSLOPE(phasein, m_phasein) * m_radtoinc);
					m_phasein = phasein;
				}
				waves[j].sin_val = lookupi1(table0, table1, waves[j].phase, lomask);
				out_val += (waves[j].sin_val * sc_reciprocal((float)m_num_waves));
				waves[j].phase += waves[j].phaseinc;
			}
			outBuf[i] = out_val;
		}

		for (int j = 0; j < m_num_waves; j++) {
			waves[j].m_phase = waves[j].phase;
		}
	}
};

PluginLoad(EmergentUGens) {
	ft = inTable;
	registerUnit<ElementaryCA>(ft, "ElementaryCA");
	registerUnit<Flock>(ft, "Flock");
}

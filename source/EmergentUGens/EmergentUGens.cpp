#include "SC_PlugIn.hpp"
#include "ElementaryCA.hpp"
#include "BoidFlock.hpp"
#include "Boid.hpp"
#include "Pvector.hpp"

using Automaton = CA<Iteration_Vector>;

// Create a global reference to world so that the RT allocator can access it.
World* g_pWorld = nullptr;

// InterfaceTable contains pointers to functions in the host (server).
InterfaceTable* ft;

// A struct to hold data used to calculate each generated sine wave value.
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
		// Initialise a pointer to the random number buffer.
		rand_buf = ctor_get_buf(in0(0));

		g_pWorld = world;

		// Initialise variables for sine wave oscillators.
		int tableSize2 = ft->mSineSize;
		m_radtoinc = tableSize2 * rtwopi * 65536.0;
		m_cpstoinc = tableSize2 * SAMPLEDUR * 65536.0;
		m_lomask = (tableSize2 - 1) << 3;
		m_phasein = 0.0;
		waves = (SinWave*)RTAlloc(world, m_num_waves * sizeof(SinWave));
		if (waves == NULL) {
			set_calc_function<EmergentUGen, &EmergentUGen::clear>();
			if (world->mVerbosity > -2) {
				Print("Failed to allocate memory for ugen.\n");
			}
			return;
		}
		for (int i = 0; i < m_num_waves; i++) {
			waves[i].m_phase = (int32)(m_phasein * m_radtoinc);
		}
	}

	~EmergentUGen() {
		RTFree(world, waves);
	}

protected:
	// The buffer that is used to provide deterministic randomised values.
	const SndBuf* rand_buf;
	// The seed used to offset the index into the buffer of random values.
	const int m_seed;
	// The number of sine waves to combine into the final output.
	const int m_num_waves;
	// Variables used to calculate the phase value of the output.
	float m_phasein;
	double m_radtoinc;
	double m_cpstoinc;
	int32 m_lomask;
	// Struct pointers to hold the state of each frequency partial.
	SinWave* waves;
	// A reference to this UGen to simplify some data access.
	const Unit* unit = this;
	// A shortcut to the world struct to simplify some further data access.
	World* world = unit->mWorld;

	// Initialise a pointer to a buffer using a buffer number sent from the SC client.
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
 
	// The below function is a shim to allow linear interpolation without C++20.
  float lerp(float low, float high, float t) {
    return low + t * (high - low);
  }
};

struct ElementaryCA : public EmergentUGen {
public:
	ElementaryCA() : EmergentUGen((int)in0(6), (int)in0(3) * (int)in0(4)) {
		m_partials_flags = (int*)RTAlloc(world, m_num_columns * m_num_rows * sizeof(int));
		if (m_partials_flags == NULL) {
			set_calc_function<EmergentUGen, &ElementaryCA::clear>();
			if (world->mVerbosity > -2) {
				Print("Failed to allocate memory for ugen.\n");
			}
			return;
		}
		if (!m_random) {
			// Initialise a list of flags to enable various frequency partials in the final output.
			decode_partials_flags();
		}
		run_automaton();
		set_calc_function<ElementaryCA, &ElementaryCA::next_a>();

		next_a(1);
	}

	~ElementaryCA() {
		RTFree(world, m_partials_flags);
	}

private:
	// Which one of the 256 elementary cellular automata to simulate.
	const int m_wolfram_code = (int)in0(2);
	// The numbers of columns and rows in the automaton's simulated 'grid' of values.
	const int m_num_columns = (int)in0(3);
	const int m_num_rows = (int)in0(4);
	// The proportional width between each possible partial in the frequency domain.
	float m_width = in0(7);
	// The amounts by which odd and even partials are skewed away from the width.
	float m_odd_skew = in0(8);
	float m_even_skew = in0(9);
	// A value that controls the amplitude decay envelope of the partials as a whole.
	float m_amp_tilt = in0(10);
	// A value that cuts the amplitude of either the odd or even partials.
	float m_balance = in0(11);
	// A flag that indicates whether or not to randomly choose the first few partials.
	const bool m_random = ((int)in0(12)) > 0;
	/*
	  If the above random flag is false, the below variable is eventually initialised
		with incoming values specifying which of the first few partials to enable/disable.
	*/
	int* m_partials_flags;
	// The cellular automaton that runs to calculate the final arrangement of partials.
	Automaton automaton;

  /*
	  The below function is a workaround to enable the current version of Sonic Pi
		at time of writing to send a 'list' of 'binary' values to this UGen, by
		encoding them as a single integer number.
		Once support has been added to Sonic Pi to allow it to send array values to
		SynthDefs without this workaround, the implementation of extracting these
		partials flags can be changed if desired.
	*/
	void decode_partials_flags() {
		int i = 0;
		int encoded_value = (int)in0(5);
		while (encoded_value >= 0 && i < m_num_columns) {
			m_partials_flags[m_num_columns - 1 - i] = encoded_value % 2;
			encoded_value = encoded_value / 2;
			i++;
		}
	}

	void run_automaton() {
		Iteration_Vector initial_state(m_num_columns);
		if (m_random) {
			for (int i = 0; i < m_num_columns; i++) {
				int flag = (buf_rand(i) <= 0.5 ? 0 : 1);
				initial_state.set(i, flag);
			}
		}
		else {
			for (int i = 0; i < m_num_columns; i++) {
				initial_state.set(i, m_partials_flags[i]);
			}
		}
		automaton.init(m_wolfram_code, m_num_columns, m_num_rows, initial_state);
		automaton.evolve();
	}

	void next_a(int inNumSamples) {
		float* table0 = ft->mSineWavetable;
		float* table1 = table0 + 1;
		float* outBuf = out(0);
		float freqin = in0(1);
		float phasein = 0.0;

		// Prevent adding partials at or above the Nyquist frequency, to avoid aliasing.
		int max_partials = (int)(((0.48 * world->mSampleRate) - freqin) / freqin);
		int active_waves = std::min(m_num_waves, max_partials);

		for (int i = 0; i < active_waves; i++) {
			waves[i].phase = waves[i].m_phase;
		}
		int32 lomask = m_lomask;

		// Store automaton state in partials flags array.
		automaton.store(m_partials_flags, active_waves);

		for (int i = 0; i < inNumSamples; ++i) {
			float out_val = 0.0;
			for (int j = 0; j < active_waves; j++) {
				if (i == 0) {
					waves[j].amp = 0.0;
						waves[j].amp = powf(j + 1, -m_amp_tilt);// * powf(e.filter, i);
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

					waves[j].i_freq = (int32)(m_cpstoinc * partial_frequency(freqin, j));
					waves[j].phaseinc = waves[j].i_freq + (int32)(CALCSLOPE(phasein, m_phasein) * m_radtoinc);
					m_phasein = phasein;
				}
				waves[j].sin_val = lookupi1(table0, table1, waves[j].phase, lomask);
				out_val += (waves[j].sin_val * waves[j].amp * m_partials_flags[j] * sc_reciprocal((float)active_waves));
				waves[j].phase += waves[j].phaseinc;
			}
			outBuf[i] = out_val;
		}

		for (int j = 0; j < active_waves; j++) {
			waves[j].m_phase = waves[j].phase;
		}
	}

	float partial_frequency(float freqin, int idx) {
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
		// Initialise a number of 'boids' in the simulation using given limits for speed.
		for (int i = 0; i < m_num_waves; i++) {
			float speed = lerp(m_min_speed, m_max_speed, buf_rand(i));
			float angle = twopi * buf_rand(i);
			float vx = speed * cos(angle);
			float vy = speed * sin(angle);
      bool predator = buf_rand(i) <= m_predator_probability;
      Boid b((WIDTH / 2.0), (HEIGHT / 2.0), vx, vy, m_max_speed, m_max_force, predator);
      flock.addBoid(b);
		}
		set_calc_function<Flock, &Flock::next_a>();
		next_a(1);
	}

private:
	// The dimensions of the simulated flock's environment.
	const float WIDTH = 1000.0;
	const float HEIGHT = 1000.0;
	/*
	  The simulated flock that has the flocking algorithm applied to it
		to generate values that modulate the UGen's basic audio frequency.
	*/
	BoidFlock flock;
	// The lower and upper bounds of the allowed starting speed to give to each boid.
  float m_min_speed = in0(5);
  float m_max_speed = in0(6);
	// The maximum amount of force (eg acceleration) that can be applied to each boid.
	float m_max_force = in0(7);
	// The specific property of the boids that is used to modulate the UGen's audio frequency.
  const int m_note_mod_source = (int)in0(8);
	// The probability that a newly created individual boid will become a 'predator'.
	const float m_predator_probability = in0(10);
	// The amount by which the chosen property of the boids affects the UGen's audio frequency.
  float m_note_mod_amount;
    
	void next_a(int inNumSamples) {
		float* table0 = ft->mSineWavetable;
		float* table1 = table0 + 1;
		float* outBuf = out(0);
		float freqin = in0(1);
		float phasein = in0(2);
		m_note_mod_amount = in0(9);
		for (int i = 0; i < m_num_waves; i++) {
			waves[i].phase = waves[i].m_phase;
		}
		int32 lomask = m_lomask;

		// Run the emergent behaviour algorithm.
		flock.flocking();
        
    float m_mod_value = 0.0;
		for (int i = 0; i < inNumSamples; ++i) {
			float out_val = 0.0;
			for (int j = 0; j < m_num_waves; j++) {
				if (i == 0) {
					/*
						Modulate the frequency with one of the following properties of the boid, multiplied by an 'amount':
						 - horizontal position
						 - vertical position
						 - speed
             - current angle of heading
          */
          switch(m_note_mod_source) {
            case 0:
							m_mod_value = flock.getBoid(j).location.x / 10.0;
              break;
            case 1:
							m_mod_value = flock.getBoid(j).location.y / 10.0;
              break;
            case 2:
							m_mod_value = flock.getBoid(j).velocity.magnitude()*50.0;
              break;
            case 3:
							m_mod_value = flock.getBoid(j).angle(flock.getBoid(j).velocity);
              break;
          }
					waves[j].i_freq = (int32)(m_cpstoinc * (freqin + (m_mod_value * m_note_mod_amount)));
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

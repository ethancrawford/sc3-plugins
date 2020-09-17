ElementaryCA : UGen {
    *ar { arg bufnum = 0, freq = 261.63, wolfram_code = 30, num_columns = 8, num_rows = 2, encoded_partials = 255, seed = 0, width = 1, odd_skew = 0, even_skew = 0, amp_tilt = 1, balance = 0, randomise = 0;
        ^this.multiNew('audio', bufnum, freq, wolfram_code, num_columns, num_rows, encoded_partials, seed, width, odd_skew, even_skew, amp_tilt, balance, randomise)
    }
}

Flock : UGen {
    *ar { arg bufnum = 0, freq = 440.0, iphase = 0.0, seed = 0, num_boids = 20, min_speed = -2.0, max_speed = 3.5, note_mod_source = 0, note_mod_amount = 1;
        ^this.multiNew('audio', bufnum, freq, iphase, seed, num_boids, min_speed, max_speed, note_mod_source, note_mod_amount)
    }
}

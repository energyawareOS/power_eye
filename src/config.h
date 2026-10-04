#ifndef _CONFIG_H_
#define _CONFIG_H_

extern unsigned long interval;
extern unsigned int timeout;
extern int has_cpu_field;
extern int has_dram;
extern int has_mmdd;
extern int has_energy;
extern int has_headers;
extern int has_self;
extern int use_bpf;
extern double coma_hours;
extern double coma_window_s, coma_delta_w, coma_deep_pct, coma_floor_w;
extern unsigned long adapt_cap_us;
extern double adapt_tol_abs_w;

#endif // _CONFIG_H_

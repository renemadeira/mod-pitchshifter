#include "PitchShifterClasses.h"
#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif

// for GetBufferSize
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>

#define N_SAMPLES_DEFAULT 256

PSAnalysis::PSAnalysis(uint32_t n_samples, int nBuffers, const char* wisdomFile) //Construtor
{
	Qcolumn = nBuffers;
	hopa = n_samples;
	N = nBuffers*n_samples;

	frames = new double[N]; fill_n(frames,N,0);
	b = new double*[hopa];

	for (int i=0 ; i< nBuffers; i++)
		b[i] = &frames[i*hopa];

	frames2 = fftwf_alloc_real(N);
	fXa = fftwf_alloc_complex(N/2 + 1);
	Xa.zeros(N/2 + 1);
	XaPrevious.zeros(N/2 + 1);
	Xa_arg.zeros(N/2 + 1);
	XaPrevious_arg.zeros(N/2 + 1);
	d_phi.zeros(N/2 + 1); 
	d_phi_prime.zeros(N/2 + 1); 
	d_phi_wrapped.zeros(N/2 + 1); 
	omega_true_sobre_fs.zeros(N/2 + 1); 
	AUX.zeros(N/2 + 1); 
	Xa_abs.zeros(N/2 + 1); 
	w.zeros(N); hann(N,&w); 
	I.zeros(N/2 + 1); I = linspace(0, N/2, N/2 + 1);

	if (fftwf_import_system_wisdom() != 0)
	{
		p = fftwf_plan_dft_r2c_1d(N, frames2, fXa, FFTW_WISDOM_ONLY|FFTW_ESTIMATE);
		printf("PSAnalysis: using system wisdom file\n");
	}
	else if (fftwf_import_wisdom_from_filename(wisdomFile) != 0)
	{
		p = fftwf_plan_dft_r2c_1d(N, frames2, fXa, FFTW_WISDOM_ONLY|FFTW_ESTIMATE);
		printf("PSAnalysis: using plugin-provided wisdom file\n");
	}
	else
	{
		p = fftwf_plan_dft_r2c_1d(N, frames2, fXa, FFTW_ESTIMATE);
		printf("PSAnalysis: failed to import wisdom file '%s', using estimate instead\n", wisdomFile);
	}
}

PSAnalysis::~PSAnalysis() //Destrutor
{
	if (p) fftwf_destroy_plan(p);
	delete[] b;
	delete[] frames;
	fftwf_free(frames2);
	fftwf_free(fXa);
	Xa.clear();
	XaPrevious.clear();
	Xa_arg.clear();
	XaPrevious_arg.clear();
	d_phi.clear(); 
	d_phi_prime.clear(); 
	d_phi_wrapped.clear(); 
	omega_true_sobre_fs.clear(); 
	AUX.clear(); 
	Xa_abs.clear(); 
	w.clear(); 
	I.clear();
}

void PSAnalysis::PreAnalysis(int nBuffers, float *in)
{
	for (int i=0; i<hopa; i++)
	{
		for (int j=0; j<(nBuffers-1); j++)
			b[j][i] = b[j+1][i];
		b[nBuffers-1][i] = in[i];
	}
}

void PSAnalysis::Analysis()
{
	//Starts now
	
	//Windowing

	double norm = sqrt( N/(2.0*hopa) );
	
	for (int i=0; i<N; i++)
		frames2[i] = frames[i]*w(i)/norm;
	
	/*Analysis*/
	if (p) fftwf_execute(p);
	
	/*Processing*/
	for (int i=0; i<(N/2 + 1); i++)
	{
		Xa(i) = cx_float(fXa[i][0], fXa[i][1]);
		angle(Xa(i), &Xa_arg(i));
		//Xa_arg(i) = angle(Xa(i));
	}

	d_phi = Xa_arg - XaPrevious_arg;
	d_phi_prime = d_phi - ((2*M_PI * hopa) / N) * I;
	AUX = floor((d_phi_prime + M_PI) / (2*M_PI));
	d_phi_wrapped = d_phi_prime - AUX * (2*M_PI);
	omega_true_sobre_fs = (2*M_PI/N) * I + d_phi_wrapped / hopa;

	Xa_abs = abs(Xa);
	XaPrevious = Xa;
	XaPrevious_arg = Xa_arg;
}

PSSinthesis::PSSinthesis(PSAnalysis *obj, const char* wisdomFile) //Construtor
{
	Qcolumn = obj->Qcolumn;
	hopa = obj->hopa;
	N = obj->N;
	omega_true_sobre_fs = &obj->omega_true_sobre_fs;
	Xa_abs = &obj->Xa_abs;
	w = &obj->w;

	first = true;
	hops = new int[Qcolumn];                       fill_n(hops,Qcolumn,hopa);
	ysaida = new double[2*N + 4*(Qcolumn-1)*hopa]; fill_n(ysaida,2*N + 4*(Qcolumn-1)*hopa,0);
	yshift = new double[hopa];                     fill_n(yshift,hopa,0);
	q = fftwf_alloc_real(N);
	fXs = fftwf_alloc_complex(N/2 + 1);
	Xs.zeros(N/2 + 1);
	Phi.zeros(N/2 + 1);
	PhiPrevious.zeros(N/2 + 1);

	if (fftwf_import_system_wisdom() != 0)
	{
		p2 = fftwf_plan_dft_c2r_1d(N, fXs, q, FFTW_WISDOM_ONLY|FFTW_ESTIMATE);
		printf("PSSinthesis: using system wisdom file\n");
	}
	else if (fftwf_import_wisdom_from_filename(wisdomFile) != 0)
	{
		p2 = fftwf_plan_dft_c2r_1d(N, fXs, q, FFTW_WISDOM_ONLY|FFTW_ESTIMATE);
		printf("PSSinthesis: using plugin-provided wisdom file\n");
	}
	else
	{
		p2 = fftwf_plan_dft_c2r_1d(N, fXs, q, FFTW_ESTIMATE);
		printf("PSSinthesis: failed to import wisdom file '%s', using estimate instead\n", wisdomFile);
	}
}

PSSinthesis::~PSSinthesis() //Destrutor
{
	delete[] hops;
	delete[] ysaida;
	delete[] yshift;
	fftwf_free(q);
	fftwf_free(fXs);
	Xs.clear();
	Phi.clear();
	PhiPrevious.clear();
	if (p2) fftwf_destroy_plan(p2);
}

void PSSinthesis::PreSinthesis()
{
	for (int k=0; k< Qcolumn-1; k++) hops[k] = hops[k+1];
}

void PSSinthesis::Sinthesis(double s)
{
    //Sinthesis, t1
    
	hops[Qcolumn-1] = round(hopa*(pow(2,(s/12))));
	
	//Some declaration
	int L;
	L = N;
	for (int i=0; i< Qcolumn-1; i++)
		L = L + hops[i];
	double r;
	int n1;
	int n2;
	double n3;
	
	//Some inicialization
	
	ysaida2 = &ysaida[L-N];

	//Sinthesis, t2

	// We can divide the code in two here

	//Sinthesis: //t2-t3 -> //t1-t2: t1 = getticks();
	//step 2, t1

	Phi = PhiPrevious + (hops[Qcolumn-1])*omega_true_sobre_fs[0] ;

	//Sinthesis: //t2-t3 -> //t1-t2: t2 = getticks();
	
	int expCompEnd = N/2 + 1;
	for (int i=0; i<expCompEnd; i++) {
		ExponencialComplexa(Phi(i), &Xs(i));	
		//Xs(i) = ExponencialComplexa(Phi(i));	
	}

	//Sinthesis: //t2-t3 -> //t1-t2: t3 = getticks();

	Xs = Xa_abs[0] % Xs;
	PhiPrevious = Phi;
	
	//Sinthesis: //t2-t3 -> //t1-t2: t4 = getticks();
	//step 2, t2

	for (int i=0; i<(N/2 + 1); i++)
	{
	    fXs[i][0] = real(Xs(i));
	    fXs[i][1] = imag(Xs(i));
	}

	//step 2, t3

	//Sinthesis, t3

	/*Synthesis*/
	if (p2) fftwf_execute(p2);

	//Sinthesis, t4
	
	double norm = N*sqrt( N/(2.0*hops[Qcolumn-1]) );

	for (int i=0; i<N; i++)
		q[i] = q[i]*w[0](i)/norm;
	
	if (first)
	{
		first = false;
		memset(ysaida,0,sizeof(double)*(L-N));
		for (int i=L-N; i<L; i++)
			ysaida[i] = q[i-(L-N)];
	}
	else
	{
		for (int i=L-N; i<L; i++)
			ysaida[i] = ysaida[i] + q[i-(L-N)];
	}

	//Sinthesis, t5
	//Linear interpolation
	r = hops[Qcolumn-1]/(1.0*hopa);

        for (int n=0; n < hopa; n++)
        {
		n3 = n*r+1;
		n1 = floor(n3);
		n2 = ceil(n3);
		yshift[n] = ysaida2[n1] + (ysaida2[n2]-ysaida2[n1])*(n3 - n1);
	}

	//Sinthesis, t6
	
	//Shift ysaida hops[0] left
	for (int i=0; i<L-hops[0]; i++)
		ysaida[i] = ysaida[i+hops[0]];
	for (int i=L-hops[0]; i<L; i++)
		ysaida[i] = 0;

	//Sinthesis, t7

	
}

int nBuffersSW(uint32_t n_samples, int c64, int c128, int c256, int c_default)
{
	switch(n_samples)
	{
		case 64:
			return c64;
		case 128:
			return c128;
		case 256:
			return c256;
		default:
			return c_default;
	}
}

float InputAbsSum(float *in, uint32_t n_samples)
{
	/* needs performance testing first. should be worth it by a tiny margin.. */
#if 0 //def __ARM_NEON__
	float32x4_t acc = vdupq_n_f32(0.0f);
	float32x4_t vec;

	for (; n_samples != 0; n_samples -= 4)
	{
		vec = vld1q_f32(in);
		acc = vaddq_f32(acc, vabsq_f32(vec));
		in += 4;
	}

	float32_t arr[4];
	vst1q_f32(arr, acc);

	return arr[0] + arr[1] + arr[2] + arr[3];
#else
	float sum_abs = 0;

	for (uint32_t i=0; i<n_samples; i++)
		sum_abs = sum_abs + abs(in[i]);

	return sum_abs;
#endif
}

uint32_t GetBufferSize(const LV2_Feature* const* features)
{
    uint32_t n_samples = N_SAMPLES_DEFAULT;

    const LV2_Options_Option* options = NULL;
    const LV2_URID_Map*       uridMap = NULL;

    for (int i=0; features[i] != NULL; ++i)
    {
        if (std::strcmp(features[i]->URI, LV2_OPTIONS__options) == 0)
            options = (const LV2_Options_Option*)features[i]->data;
        else if (std::strcmp(features[i]->URI, LV2_URID__map) == 0)
            uridMap = (const LV2_URID_Map*)features[i]->data;
    }

    if (options == NULL || uridMap == NULL)
        return n_samples;

    for (int i=0; options[i].key != 0; ++i)
    {
        if (options[i].key != uridMap->map(uridMap->handle, LV2_BUF_SIZE__maxBlockLength))
            continue;
        if (options[i].type == uridMap->map(uridMap->handle, LV2_ATOM__Int))
            continue;

        const int value(*(const int*)options[i].value);
        n_samples = value;
        break;
    }

    return n_samples;
}

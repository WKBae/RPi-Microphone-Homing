/*
 Copyright (c) 2017, William Bae.
 All rights reserved.
*/

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <math.h>

#include <alsa/asoundlib.h>

#include "mailbox.h"
#include "gpu_fft.h"

#define BUFFER_SIZE 30

enum {
	ERROR_PIPE = 1,
	ERROR_FORK,
	ERROR_SIGNAL,
	ERROR_V3D,
	ERROR_LOG2,// 8 ~ 22
	ERROR_GPU_OOM,
	ERROR_VIDEOCORE,
	ERROR_LIBBCM_HOST,
	ERROR_ALSA_DEVICE,
	ERROR_ALSA_HWPARAMS_ALLOC,
	ERROR_ALSA_HWPARAMS_INIT,
	ERROR_ALSA_HWPARAMS_ACCESS_TYPE,
	ERROR_ALSA_HWPARAMS_FORMAT,
	ERROR_ALSA_HWPARAMS_RATE,
	ERROR_ALSA_HWPARAMS_CHANNELS,
	ERROR_ALSA_HWPARAMS_SET,
	ERROR_ALSA_PREPARE,
};

void do_fft();

void quit(int exit_code);

int fd[2];
int is_parent = 1;
pid_t another_pid;

void signal_quit(int signo) {
	quit(0);
}

int pid_received = 0;
void parent_sigusr1(int signo) {
	pid_received = 1;
}

volatile int angle_write_count = 0;
void child_sigusr1(int signo) {
	angle_write_count++;
}

int mic_ports[] = {0, 1, 2};
int frequency_band;
int main(int argc, char* argv[]) {
	char buffer[BUFFER_SIZE];
	int state;
	fprintf(stderr, "argc: %d\n", argc);
	if(argc > 3) {
		int i;
		for(i = 0; i < 3; i++) {
			mic_ports[i] = atoi(argv[1+i]);
			fprintf(stderr, "Setting mic#%d as %d\n", i, mic_ports[i]);
		}
		if(argc > 4)
			frequency_band = atoi(argv[4]);
	} else if(argc > 1) {
		frequency_band = atoi(argv[1]);
	} else {
		frequency_band = 836; // 18000Hz
	}

	state = pipe(fd);
	if(state == -1) {
		quit(ERROR_PIPE);
	}

	another_pid = fork();

	if(another_pid < 0) quit(ERROR_FORK);

	if(signal(SIGINT, signal_quit) == SIG_ERR) quit(ERROR_SIGNAL);
	if(signal(SIGTERM, signal_quit) == SIG_ERR) quit(ERROR_SIGNAL);
	//if(signal(SIGKILL, signal_quit) == SIG_ERR) quit(ERROR_SIGNAL);

	if(another_pid == 0) { // child
		is_parent = 0;
		read(fd[0], &another_pid, sizeof(pid_t));
		close(fd[0]);

		if(signal(SIGUSR1, child_sigusr1) == SIG_ERR) quit(ERROR_SIGNAL);
		kill(another_pid, SIGUSR1);

		do_fft();
	} else { // parent
		pid_t my_pid = getpid();
		float angle;

		if(signal(SIGUSR1, parent_sigusr1) == SIG_ERR) quit(ERROR_SIGNAL);

		write(fd[1], &my_pid, sizeof(pid_t));
		while(!pid_received) usleep(100);

		close(fd[1]);
		
		while(getchar() != 'q') {
			kill(another_pid, SIGUSR1);
			read(fd[0], &angle, sizeof(angle));
			fprintf(stdout, "%f\n", angle);
			fflush(stdout);
		}

		quit(0);
	}
}

#define PCM_FORMAT SND_PCM_FORMAT_S16_LE

#define LOG2_N 11
#define N (1 << LOG2_N)

//#define FREQ_BAND 836

#define PI 3.141592

#define DEG(x) ((x) * 360 / (2 * PI))
#define RAD(x) ((x) * 2 * PI / 360)
#define MIC_LEFT 0
#define MIC_RIGHT 1
#define MIC_BACK 2

#define SMOOTH_COUNT 3

snd_pcm_t* prepare_alsa(int hw_id);

struct GPU_FFT *fft;
snd_pcm_t* alsa_handles[3];
char* buffers[3];

void do_fft() {
	int i, j, k;
	int ret, err, mb = mbox_open();
	float results[3] = {0,0,0}, x, y;
	int skipped[3] = {0, 0, 0};

	float smooth_sum;
	int smooth_count;
	float smoothed_result[N];

	float res;
	int resi;
	
	float distance;
	float angle = 0, newangle;

	struct GPU_FFT_COMPLEX *base;
	
	int buffer_frames;
	int available_frames;
	int prev_count;

	buffer_frames = N;
	
	for(i = 0; i < 3; i++) {
		alsa_handles[i] = prepare_alsa(mic_ports[i]);
		buffers[i] = malloc(buffer_frames * snd_pcm_format_width(PCM_FORMAT) / 8);
	}
	
	ret = gpu_fft_prepare(mb, LOG2_N, GPU_FFT_REV, 3, &fft);
	switch(ret) {
		case -1: exit(ERROR_V3D); return;
		case -2: exit(ERROR_LOG2); return;
		case -3: exit(ERROR_GPU_OOM); return;
		case -4: exit(ERROR_VIDEOCORE); return;
		case -5: exit(ERROR_LIBBCM_HOST); return;
	}

	for(;;) {
		for(j = 0; j < 3; j++) {
			base = fft->in + j * fft->step;

			prev_count = angle_write_count;
			if((err = snd_pcm_readi (alsa_handles[j], buffers[j], buffer_frames)) != buffer_frames) {
				if(angle_write_count != prev_count) continue; // signal interrupt error
				fprintf (stderr, "Read from audio interface #%d failed (%s)\n",
					j, snd_strerror (err));
				//exit (10);
				if((err = snd_pcm_recover(alsa_handles[j], err, 0)) != 0) {
					fprintf(stderr, "Failed to recover error(%s)\n", snd_strerror(err));
				}
			}
			
			available_frames = snd_pcm_avail(alsa_handles[j]);
//			printf("(%d) Available: %d\n", j, available_frames);
			if(available_frames > buffer_frames * 5) {
				available_frames -= snd_pcm_forward(alsa_handles[j], available_frames - buffer_frames * 5);
			}

			for(i = 0; i < N; i++) {
				base[i].re = buffers[j][i * snd_pcm_format_width(PCM_FORMAT) / 8];
				base[i].im = 0;
			}
		}
	
		usleep(1);
		gpu_fft_execute(fft);
	
		for(j = 0; j < 3; j++) {
			base = fft->out + j * fft->step;

			/*smooth_sum = 0;
			smooth_count = 0;
			for(i = 0; i < N; i++) {
				float mag = sqrt(base[i].re * base[i].re + base[i].im * base[i].im) / 100;
				smooth_sum += mag;
				smooth_count++;
				if(i >= SMOOTH_COUNT / 2) {
					float prevR = base[i - SMOOTH_COUNT/2].re,
						prevI = base[i - SMOOTH_COUNT/2].im;
					smooth_sum -= sqrt(prevR * prevR + prevI * prevI) / 100;
					smooth_count--;
				}
				smoothed_result[i - smooth_count/2] = smooth_sum / smooth_count;
			}

			for(i = N; i < N + SMOOTH_COUNT / 2; i++) {
				float prevR = base[i - SMOOTH_COUNT/2].re,
					prevI = base[i - SMOOTH_COUNT/2].im;
				smooth_sum -= sqrt(prevR * prevR + prevI * prevI) / 100;
				smooth_count--;
				smoothed_result[i - smooth_count/2] = smooth_sum / smooth_count;
			}*/

			res = 0;
			resi = 0;
			for(i = frequency_band - 10; i <= frequency_band + 10; i++) {
				float mag = sqrt(base[i].re * base[i].re + base[i].im * base[i].im);
				//fprintf(stderr, "%d(%d): %f\n", j, i, mag);
				//if(mag > 10 && mag < 100) {
					res += mag;
					resi++;
				//}
			}
			if(resi > 0) {
				float diff = abs(results[j] - (res / resi));
				if(diff < 1500 || skipped[j] > 30) {
					results[j] = results[j] * 0.7f + (res / resi) * 0.3f;
					skipped[j] = 0;
				} else {
					skipped[j]++;
				}
			}

		}
/*		if(results[MIC_RIGHT] > results[MIC_BACK]) {
			if(results[MIC_RIGHT] > results[MIC_LEFT]) {
				putchar('R');
			} else {
				putchar('L');//putchar('B');
			}
		} else {
			if(results[MIC_BACK] > results[MIC_LEFT]) {
				putchar('B');//putchar('G');
			} else {
				putchar('L');//putchar('B');
			}
		}
		printf("\n");*/
		x = 1.41421356237 / 2 * (results[MIC_RIGHT] - results[MIC_LEFT]);
		y = (results[MIC_RIGHT] + results[MIC_LEFT]) / 2 - results[MIC_BACK];
		distance = x * x + y * y;
		
		if(distance < 10) {
			//angle = newangle * 0.15f; // no change
		} else {
			//newangle = 180 - (int)(90 + atan2(y, x) * 360 / (2 * PI) + 360.5) % 360;
			newangle = fmod(90 + atan2(y, x) * 360 / (2 * PI) + 360, 360) - 180;
			//angle = angle * 0.85f + newangle * 0.15f;
			angle = angle * 0.5f + newangle * 0.5f;
		}
//		printf("\t%f\n", angle);
		
		for(; angle_write_count > 0; angle_write_count--) {
			write(fd[1], &angle, sizeof(angle));
//			fprintf(stderr, "(%f %f %f) %f, %f = %f\n", results[0], results[1], results[2], x, y, distance);
		}
		usleep(46 * 5);
	}
//	printf("Exited loop\n");
	quit(0);
}
		

snd_pcm_t* prepare_alsa(int hw_id) {
	int err;
	unsigned int rate = 44100;
	snd_pcm_t *capture_handle;
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_format_t format = PCM_FORMAT;
	char hw[5] = "hw:0";
	
	hw[3] = hw_id % 10 + '0';
	if ((err = snd_pcm_open (&capture_handle, hw, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		fprintf (stderr, "cannot open audio device %s (%s)\n", 
			hw,
			snd_strerror (err));
		quit(ERROR_ALSA_DEVICE);
	}
	
//	fprintf(stdout, "audio interface opened\n");
	
	if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
		fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_ALLOC);
	}
	
//	fprintf(stdout, "hw_params allocated\n");
		 
	if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
		fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_INIT);
	}
	
//	fprintf(stdout, "hw_params initialized\n");
	
	if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		fprintf (stderr, "cannot set access type (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_ACCESS_TYPE);
	}
	
//	fprintf(stdout, "hw_params access setted\n");
	
	if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, format)) < 0) {
		fprintf (stderr, "cannot set sample format (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_FORMAT);
	}
	
//	fprintf(stdout, "hw_params format setted\n");
	
	if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, 0)) < 0) {
		fprintf (stderr, "cannot set sample rate (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_RATE);
	}
	
//	fprintf(stdout, "hw_params rate setted\n");
	
	if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 1)) < 0) {
		fprintf (stderr, "cannot set channel count (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_CHANNELS);
	}
	
//	fprintf(stdout, "hw_params channels setted\n");
	
	if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
		fprintf (stderr, "cannot set parameters (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_HWPARAMS_SET);
	}
	
//	fprintf(stdout, "hw_params setted\n");
	
	snd_pcm_hw_params_free (hw_params);
	
//	fprintf(stdout, "hw_params freed\n");
	
	if ((err = snd_pcm_prepare (capture_handle)) < 0) {
		fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
			snd_strerror (err));
		quit(ERROR_ALSA_PREPARE);
	}
	
//	fprintf(stdout, "audio interface prepared\n");
	
	return capture_handle;
}

struct GPU_FFT *fft;
snd_pcm_t* alsa_handles[3];
char* buffers[3];

void quit(int exit_code) {
	if(!is_parent) {
		int i;
		if(fft) gpu_fft_release(fft);
		for(i = 0; i < 3; i++) {
			free(buffers[i]);
			if(alsa_handles[i]) snd_pcm_close(alsa_handles[i]);
		}
	}

	if(another_pid) kill(another_pid, SIGINT);
	exit(exit_code);
}

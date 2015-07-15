#include <3ds.h>
#include <cstdio>
#include "stb_vorbis.h"

#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

#include <GL/gl.h>
#include "gfx_device.h"

#include "stb_image.h"
#include "play_3_png.h"
#include "pause_2_png.h"
#include "loop_png.h"
#include "loop_disable_png.h"
#include "power_png.h"

#include "MB_Gothic_Spell_ttf.h"
#include "Anita_ttf.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <FLAC/stream_decoder.h>

stb_vorbis *v = NULL;

enum {
   STATE_IDLE,
   STATE_FC
};

u32 state = STATE_IDLE;
char *filename;
std::string cur_dir = "";
u32 cursor_pos = 0;
bool draw_ui = true;
int error;
s16 *audiobuf = NULL;
stb_vorbis_info info;
FLAC__StreamDecoder *FLAC_decoder = NULL;
u32 Samples;
u32 audiobuf_size;
bool loop_flag = false;
bool paused = false;
u8 power_level = 5;
u8 is_charging = 0;
float translate = 0.0;

std::string currently_playing;

enum {
   AUDIO_MODE_VORBIS,
   AUDIO_MODE_FLAC,
};

u32 decode_mode = AUDIO_MODE_VORBIS;

bool is_dir(u32 s) {
   if (s == 0) return true;
   DIR *dir;
   struct dirent *ent;
   if ((dir = opendir (cur_dir.c_str())) != NULL) {

      u32 cur = 1;
      while ((ent = readdir (dir)) != NULL) {
         if (strncmp(ent->d_name, ".", 1) == 0) continue;
         if (cur == s) {
            struct stat st;
            std::string name = cur_dir + "/" + ent->d_name;
            stat(name.c_str(), &st);
            closedir (dir);
            if(S_ISDIR(st.st_mode))
               return true;
            else
               return false;
         }
         cur++;
      }
      closedir (dir);
   } else {
      /* could not open directory */
     perror ("");
   }
   return false;
}

void print(float x, float y, const std::string str);

GLuint fc_list = 0;

void filechooser() {
   if (fc_list == 0) {
      fc_list = glGenLists(1);
   }
   float bias = (float)(cursor_pos * 10);
   if (bias + translate >= 220) {
      translate -= (bias + translate) - 220.0;
   }

   if (bias + translate < 10) {
      translate += 10.0;
   }

   glLoadIdentity();
   glScalef(1.0/400.0, 1.0/240.0, 1.0);
   glTranslatef(0, translate, 0);

   if (draw_ui) {
      glNewList(fc_list, GL_COMPILE_AND_EXECUTE);
      DIR *dir;
      struct dirent *ent;
      if ((dir = opendir (cur_dir.c_str())) != NULL) {
        /* print all the files and directories within directory */
         float ycoord = 10.0;
         u32 cur = 0;
         if (cur == cursor_pos) {
            glColor4f(0.1, 1, 1, 1.0);
         }
         print (0, ycoord, "..\n");
         if (cur == cursor_pos) {
            glColor4f(1, 1, 1, 1);
         }
         ++cur;


         while ((ent = readdir (dir)) != NULL) {
            if (strncmp(ent->d_name, ".", 1) == 0) continue;
            if (cur == cursor_pos) {
               glColor4f(0.1, 1, 1, 1.0);
            }
            ycoord += 10.0;
            print (0, ycoord, ent->d_name);
            if (cur == cursor_pos) {
               glColor4f(1, 1, 1, 1);
            }
            cur++;

         }
         closedir (dir);
      } else {
        /* could not open directory */
        perror ("");
      }
      glEndList();
      draw_ui = false;
   } else {
      glCallList(fc_list);
   }

}

void cd(){
   if (cursor_pos == 0) {
      if (cur_dir.compare("/") != 0) {
         cur_dir = cur_dir.substr(0, cur_dir.rfind("/"));
      }
      return;
   }
   DIR *dir;
   struct dirent *ent;
   if ((dir = opendir (cur_dir.c_str())) != NULL) {

      u32 cur = 1;
      while ((ent = readdir (dir)) != NULL) {
         if (strncmp(ent->d_name, ".", 1) == 0) continue;
         if (cur == cursor_pos) {
            cur_dir = cur_dir + "/" + ent->d_name;
            closedir (dir);
            return;
         }
         cur++;
      }
      closedir (dir);
   } else {
     /* could not open directory */
     perror ("");
   }
}
u32 audiobuf_index = 0;
FLAC__StreamDecoderWriteStatus FLAC_write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{

	/* write decoded PCM samples */
	for(u32 i = 0; i < frame->header.blocksize; i++) {
		// if(
		// 	!write_little_endian_int16(f, (FLAC__int16)buffer[0][i]) ||  /* left channel */
		// 	!write_little_endian_int16(f, (FLAC__int16)buffer[1][i])     /* right channel */
		// ) {
		// 	fprintf(stderr, "ERROR: write error\n");
		// 	return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
		// }
      audiobuf[audiobuf_index] = (FLAC__int16)buffer[0][i];
      audiobuf[audiobuf_index + 1] = (FLAC__int16)buffer[1][i];
      audiobuf_index += 2;
	}

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void FLAC_metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	/* print some stats */
	// if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		/* save for later */
		// total_samples = metadata->data.stream_info.total_samples;
		Samples = metadata->data.stream_info.sample_rate;
      audiobuf_size = Samples * sizeof(s16) * 2;
      audiobuf = (s16*)linearAlloc(audiobuf_size);
	// }
}

void FLAC_error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	(void)decoder, (void)client_data;

	fprintf(stderr, "Got error callback: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
}

void play_file_from_filename(const std::string name) {
   currently_playing = std::string(name);
   if (audiobuf) linearFree(audiobuf);

   if (name.rfind(".flac") != std::string::npos) {
      if (!FLAC_decoder) {
         FLAC_decoder = FLAC__stream_decoder_new();
         FLAC__stream_decoder_set_md5_checking(FLAC_decoder, true);
      }
      audiobuf_index = 0;
      decode_mode = AUDIO_MODE_FLAC;
      FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_file(FLAC_decoder, name.c_str(), FLAC_write_callback, FLAC_metadata_callback, FLAC_error_callback, NULL);
      if(init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
         printf("ERROR: initializing decoder: %s\n", FLAC__StreamDecoderInitStatusString[init_status]);
      }
      FLAC__stream_decoder_process_until_end_of_metadata(FLAC_decoder);
   } else {
      decode_mode = AUDIO_MODE_VORBIS;
      v = stb_vorbis_open_filename(name.c_str(), &error, NULL);
      info = stb_vorbis_get_info(v);
      Samples = info.sample_rate;
      audiobuf_size = Samples * sizeof(s16) * 2;
      audiobuf = (s16*)linearAlloc(audiobuf_size);
   }
   paused = false;
}

void play_file() {
   DIR *dir;
   struct dirent *ent;
   if ((dir = opendir (cur_dir.c_str())) != NULL) {

      u32 cur = 1;
      while ((ent = readdir (dir)) != NULL) {
         if (strncmp(ent->d_name, ".", 1) == 0) continue;
         if (cur == cursor_pos) {
            if (v) {
               stb_vorbis_close(v);
            }
            std::string name = cur_dir + "/" + ent->d_name;
            play_file_from_filename(name);
            closedir (dir);
            return;
         }
         cur++;
      }
      closedir (dir);
   } else {
      /* could not open directory */
      perror ("");
   }
}

void pick() {
   if (is_dir(cursor_pos)) {
      cd();
      cursor_pos = 0;
   } else {
      play_file();
      state = STATE_IDLE;
   }
}

void draw_string_goth(float x, float y, const std::string str);

std::string to_string(int number)
{
   std::stringstream ss;//create a stringstream
   ss << number;//add number to the stream
   return ss.str();//return a string with the contents of the stream
}

void print_menu() {
   if (true) {
      // consoleClear();
      glClear(GL_COLOR_BUFFER_BIT);
      glColor4f(1, 1, 1, 1);
      if (state == STATE_IDLE) {
         print(0, 10, "OGG player by machinamentum\n");
         print(0, 30, "File controls:\n");
         print(0, 40, "X = Switch to/from file chooser\n");
         print(0, 50, "A = Choose file\n");

         print(0, 70, "Player controls/info:");
         if (audiobuf) {
            print(0, 80, "Currently playing: " + currently_playing);
         }
         print(0, 90, std::string("Loop [Toggle DPAD-UP]: ") + (loop_flag ? "loop song\n" : "off\n"));
         print(0, 100, std::string("Play/Pause [Toggle A]: ") + (paused ? "Paused\n" : "Playing\n"));
         print(0, 110, "Battery: " + to_string(power_level));
         print(0, 120, std::string("Charging: ") + (is_charging ? "charging\n" : "not charging\n"));
         print(0, 130, std::string("Decoding mode: ") + (decode_mode == AUDIO_MODE_VORBIS ? "OGG Vorbis\n" : "FLAC\n"));
         print(0, 140, "Sample Rate: " + to_string(Samples));
      } else if (state == STATE_FC) {
         filechooser();
      }
   }
}


struct Box {
   int x, y, x2, y2;
   Box(int px, int py, int width, int height) {
      x = px;
      y = py;
      x2 = x + width;
      y2 = y + height;
   }

   bool is_within(int x0, int y0) {
      return x0 >= x && x0 <= x2 && y0 >= y && y0 <= y2;
   }
};

Box play_box = Box(320 / 2 - 32, 240 / 2 - 32, 64, 64);
Box loop_box = Box(320 / 2 - 32 - 96, 240 / 2 - 32, 64, 64);

void state_man() {
   print_menu();
   u32 kDown = hidKeysDown();
   //hidKeysHeld returns information about which buttons have are held down in this frame
   u32 kHeld = hidKeysHeld();
   //hidKeysUp returns information about which buttons have been just released
   u32 kUp = hidKeysUp();

   touchPosition touch;

   //Read the touch screen coordinates
   hidTouchRead(&touch);

   if (play_box.is_within(touch.px, touch.py) && (kDown & KEY_TOUCH)) {
      paused = !paused;
      draw_ui = true;
   }

   if (loop_box.is_within(touch.px, touch.py) && (kDown & KEY_TOUCH)) {
      loop_flag = !loop_flag;
      draw_ui = true;
   }

   if (state == STATE_IDLE) {
      if (kDown & KEY_X) {
         state = STATE_FC;
         cursor_pos = 0;
         draw_ui = true;
      }

      if (kUp & KEY_UP) {
         loop_flag = !loop_flag;
         draw_ui = true;
      }
      if (kUp & KEY_A) {
         paused = !paused;
         draw_ui = true;
      }
   } else if (state == STATE_FC) {
      if (kDown & KEY_X) {
         state = STATE_IDLE;
         draw_ui = true;
      }

      if (kUp & KEY_UP) {
         cursor_pos--;
         if (cursor_pos < 0) cursor_pos = 0;
         draw_ui = true;
      }
      if (kDown & KEY_DOWN) {
         cursor_pos++;
         if (cursor_pos < 0) cursor_pos = 0;
         draw_ui = true;
      }

      if (kUp & KEY_A) {
         pick();
         draw_ui = true;
      }
   }
}


extern "C" {
extern void glFrustumf (GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
}

void draw_unit_square() {
   glPushMatrix();
   glTranslatef(0.0, 0.0, -0.5);
   glBegin(GL_TRIANGLES);
      glTexCoord2f(0.0f, 0.0f);
      glVertex3f(0.0f, 0.0f, 0.0f);

      glTexCoord2f(0.0f, 1.0f);
      glVertex3f(0.0f, 1.0f, 0.0f);

      glTexCoord2f(1.0f, 1.0f);
      glVertex3f(1.0f, 1.0f, 0.0f);

      glTexCoord2f(0.0f, 0.0f);
      glVertex3f(0.0f, 0.0f, 0.0f);

      glTexCoord2f(1.0f, 1.0f);
      glVertex3f(1.0f, 1.0f, 0.0f);

      glTexCoord2f(1.0f, 0.0f);
      glVertex3f(1.0f, 0.0f, 0.0f);
   glEnd();
   glPopMatrix();
}

void *device = NULL;
GLuint play_button;
GLuint pause_button;
GLuint loop_button;
GLuint loop_disable_button;
GLuint power_icon;

unsigned char temp_bitmap[512*512];

stbtt_bakedchar cdata_goth[96]; // ASCII 32..126 is 95 glyphs
GLuint goth_tex;

stbtt_bakedchar cdata_console[96]; // ASCII 32..126 is 95 glyphs
GLuint console_tex;

void initfont_goth(void)
{
	// fread(ttf_buffer, 1, 1<<20, fopen("sdmc:/3ds/selena/assets/aladdin.ttf", "rb"));
	stbtt_BakeFontBitmap((unsigned char*)MB_Gothic_Spell_ttf,0, 20.0, temp_bitmap,512,512, 32,96, cdata_goth); // no guarantee this fits!
	// can free ttf_buffer at this point
	glGenTextures(1, &goth_tex);
	glBindTexture(GL_TEXTURE_2D, goth_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512,512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
	// can free temp_bitmap at this point
}

void initfont_anita(void)
{
	// fread(ttf_buffer, 1, 1<<20, fopen("sdmc:/3ds/selena/assets/aladdin.ttf", "rb"));
	stbtt_BakeFontBitmap((unsigned char*)Anita_ttf,0, 12.0, temp_bitmap,512,512, 32,96, cdata_console); // no guarantee this fits!
	// can free ttf_buffer at this point
	glGenTextures(1, &console_tex);
	glBindTexture(GL_TEXTURE_2D, console_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512,512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
	// can free temp_bitmap at this point
}

void print(float x, float y, const std::string str)
{
	// assume orthographic projection with units = screen pixels, origin at top left
	glBindTexture(GL_TEXTURE_2D, console_tex);
	glBegin(GL_QUADS);
   const char *text = str.c_str();
	while (*text) {
			if (*text >= 32 && (unsigned char)(*text) < 128) {
				stbtt_aligned_quad q;
				stbtt_GetBakedQuad(cdata_console, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
				glTexCoord2f(q.s0,q.t0); glVertex2f(q.x0,q.y0);
				glTexCoord2f(q.s1,q.t0); glVertex2f(q.x1,q.y0);
				glTexCoord2f(q.s1,q.t1); glVertex2f(q.x1,q.y1);
				glTexCoord2f(q.s0,q.t1); glVertex2f(q.x0,q.y1);
			}
			++text;
	}
	glEnd();
}

void draw_string_goth(float x, float y, const std::string str)
{
	// assume orthographic projection with units = screen pixels, origin at top left
	glBindTexture(GL_TEXTURE_2D, goth_tex);
	glBegin(GL_QUADS);
   const char *text = str.c_str();
	while (*text) {
			if (*text >= 32 && (unsigned char)(*text) < 128) {
				stbtt_aligned_quad q;
				stbtt_GetBakedQuad(cdata_goth, 512,512, *text-32, &x,&y,&q,1);//1=opengl & d3d10+,0=d3d9
				glTexCoord2f(q.s0,q.t0); glVertex2f(q.x0,q.y0);
				glTexCoord2f(q.s1,q.t0); glVertex2f(q.x1,q.y0);
				glTexCoord2f(q.s1,q.t1); glVertex2f(q.x1,q.y1);
				glTexCoord2f(q.s0,q.t1); glVertex2f(q.x0,q.y1);
			}
			++text;
	}
	glEnd();
}


void render_power() {
   glLoadIdentity();
   glScalef(1.0f/320.0f, 1.0/240.0f, 1.0f);
   glTranslatef(320.0f - 65.0f + 16.0f, 8.0f, 0.0f);
   glScalef((32.0f / 5.0f) * ((float)power_level), 16.0f, 1.0f);
   glDisable(GL_TEXTURE_2D);
   if (power_level <= 2) {
      glColor4f(1.0f, 0.0f, 0.0f, 0.8f);
   } else {
      glColor4f(0.0f, 1.0f, 0.0f, 0.8f);
   }

   if (is_charging) {
      glColor4f(0.0f, 0.0f, 1.0f, 0.8f);
   }

   draw_unit_square();
}

void render() {
   // glClearColor(223.0f/256.0f, 193.0f/256.0f, 42.0f/256.0f, 1.0f);
   glClearColor(0,0,0,1);
   glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_BLEND);
   glEnable(GL_TEXTURE_2D);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glScalef(1.0f/320.0f, 1.0/240.0f, 1.0f);
   glTranslatef(320.0f / 2.0f - 32.0f, 240.0f / 2.0f - 32.0f, 0.0f);
   glScalef(64.0f, 64.0f, 1.0f);


   glBindTexture(GL_TEXTURE_2D, (paused ? play_button : pause_button));
   draw_unit_square();

   glLoadIdentity();
   glScalef(1.0f/320.0f, 1.0/240.0f, 1.0f);
   glTranslatef(320.0f / 2.0f - 32.0f - 96.0f, 240.0f / 2.0f - 32.0f, 0.0f);
   glScalef(64.0f, 64.0f, 1.0f);

   glBindTexture(GL_TEXTURE_2D, (loop_flag ? loop_button : loop_disable_button));
   draw_unit_square();

   glLoadIdentity();
   glScalef(1.0f/320.0f, 1.0/240.0f, 1.0f);
   glTranslatef(320.0f - 64.0f, -16.0f, 0.0f);
   glScalef(64.0f, 64.0f, 1.0f);

   glBindTexture(GL_TEXTURE_2D, power_icon);
   draw_unit_square();
   render_power();


}

int main()
{
   // Initialize services
   srvInit();
   aptInit();
   hidInit(NULL);
   ptmInit();
   gfxInitDefault();
   // consoleInit(GFX_TOP, NULL);
   csndInit();
   // fsInit();
   // sdmcInit();

   chdir("sdmc:/");

   void *device = gfxCreateDevice(240, 320);
   gfxMakeCurrent(device);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);

   glTranslatef(0.5f, 0.5f, 0.0f);
   glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
   glTranslatef(-0.5f, -0.5f, 0.0f);

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   glGenTextures(1, &play_button);
   glGenTextures(1, &pause_button);
   glGenTextures(1, &loop_button);
   glGenTextures(1, &loop_disable_button);
   glGenTextures(1, &power_icon);

   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

   int x, y, n;
   u8* image_data = stbi_load_from_memory(play_3_png, play_3_png_size, &x, &y, &n, 4);
   glBindTexture(GL_TEXTURE_2D, play_button);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
   stbi_image_free(image_data);

   image_data = stbi_load_from_memory(pause_2_png, pause_2_png_size, &x, &y, &n, 4);
   glBindTexture(GL_TEXTURE_2D, pause_button);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
   stbi_image_free(image_data);

   image_data = stbi_load_from_memory(loop_png, loop_png_size, &x, &y, &n, 4);
   glBindTexture(GL_TEXTURE_2D, loop_button);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
   stbi_image_free(image_data);

   image_data = stbi_load_from_memory(loop_disable_png, loop_disable_png_size, &x, &y, &n, 4);
   glBindTexture(GL_TEXTURE_2D, loop_disable_button);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
   stbi_image_free(image_data);

   image_data = stbi_load_from_memory(power_png, power_png_size, &x, &y, &n, 4);
   glBindTexture(GL_TEXTURE_2D, power_icon);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
   stbi_image_free(image_data);

   glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   void *top_device = gfxCreateDevice(240, 400);
   gfxMakeCurrent(top_device);
   initfont_goth();
   initfont_anita();
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);

   glTranslatef(0.5f, 0.5f, 0.0f);
   glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
   glTranslatef(-0.5f, -0.5f, 0.0f);

   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glEnable(GL_BLEND);
   glEnable(GL_TEXTURE_2D);

   int frames = 0;
   int channel = 0x8;


   while (aptMainLoop())
   {
      gspWaitForVBlank();
      hidScanInput();
      gfxMakeCurrent(top_device);
      glClear(GL_COLOR_BUFFER_BIT);
      glLoadIdentity();
      glScalef(1.0/400.0, 1.0/240.0, 1.0);
      state_man();
      gfxFlush(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL));
      u8 plevel;
      u8 charge;
      PTMU_GetBatteryLevel(NULL, &plevel);
      PTMU_GetBatteryChargeState(NULL, &charge);
      if (plevel != power_level) {
         power_level = plevel;
         // draw_ui = true;
      }
      if (charge != is_charging) {
         is_charging = charge;
         //draw_ui = true;
      }

      gfxMakeCurrent(device);
      u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
      render();
      gfxFlush(fb);
      // Your code goes here

      u32 kDown = hidKeysDown();
      if (kDown & KEY_START)
         break; // break in order to return to hbmenu

      // Flush and swap framebuffers
      if (audiobuf && !paused) {
         if(frames >= 50) {
            frames = 0;
            int n = 0;
            if (decode_mode == AUDIO_MODE_VORBIS) {
               n = stb_vorbis_get_samples_short_interleaved(v, 2, audiobuf, Samples * 2);
            } else {
               while (audiobuf_index < Samples * 2) {
                  n = FLAC__stream_decoder_process_single(FLAC_decoder);
                  if (FLAC__stream_decoder_get_state(FLAC_decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) break;
               }
               audiobuf_index = 0;
            }

            if(n == 0) {
               if (decode_mode == AUDIO_MODE_VORBIS) {
                  stb_vorbis_close(v);
               } else {
                  FLAC__stream_decoder_delete(FLAC_decoder);
               }
               linearFree(audiobuf);
               audiobuf = NULL;
               v = NULL;
               FLAC_decoder = NULL;
               if (loop_flag) play_file_from_filename(currently_playing);
            }

            GSPGPU_FlushDataCache(NULL, (u8*)audiobuf, audiobuf_size);
            if (channel == 0x8) channel = 0x6;
            if (channel == 0x6) channel = 0x8;
            csndPlaySound(SOUND_CHANNEL(channel), SOUND_ONE_SHOT | SOUND_LINEAR_INTERP | SOUND_FORMAT_16BIT, Samples * 2, 10.0, 0.0, (u32*)audiobuf, (u32*)audiobuf, audiobuf_size);

         }
         frames++;
      }

      gfxFlushBuffers();
      gfxSwapBuffersGpu();
   }

   // Exit services
   if (FLAC_decoder) {
      FLAC__stream_decoder_delete(FLAC_decoder);
   }
   csndExit();
   gfxExit();
   ptmExit();
   hidExit();
   aptExit();
   srvExit();
   return 0;
}

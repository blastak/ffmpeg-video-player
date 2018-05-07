#include "common.h"

#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

AVPacket packet;
AVCodecContext  *pCodecCtx;
AVFrame *pFrame, *pFrameRGB;
AVFormatContext *pFormatCtx;
int nVSI;

std::string inputVid;
int inputWidth;
int inputHeight;
std::string strFormatNames;
int g_vidPropInterval; ///< Frame Interval (Millisecond)

void ffmpeg_initialize()
{
	//av_log_set_level(AV_LOG_DEBUG);
	av_register_all();
	//avdevice_register_all();
	avcodec_register_all();
	avformat_network_init(); // for rtsp ????
}

void ffmpeg_finalize()
{
	av_free_packet(&packet);
	avcodec_close(pCodecCtx);
	av_free(pFrame);
	av_free(pFrameRGB);
	avformat_close_input(&pFormatCtx);

	//avformat_network_deinit();
}

bool ffmpeg_open_file()
{
	AVDictionary* d = NULL;           // "create" an empty dictionary
	av_dict_set(&d, "stimeout", "3000000", 0); // 3 sec
	av_dict_set(&d, "allowed_media_types", "video", 0); // video only
	av_dict_set(&d, "rtsp_transport", "tcp", 0); // prefer tcp
	av_dict_set(&d, "buffer_size", "20971520", 0); // 20 MB

	pFormatCtx = avformat_alloc_context();
	if (avformat_open_input(&pFormatCtx, inputVid.c_str(), NULL, &d) != 0)
	{
		LOG_("avformat_open_input error");
		return false;
	}

	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		LOG_("avformat_find_stream_info error");
		return false;
	}

	av_dump_format(pFormatCtx, 0, inputVid.c_str(), 0); // 콘솔 출력

	nVSI = -1;

	//for (int i = 0; i < pFormatCtx->nb_streams; i++)
	//{
	//	if (pFormatCtx->streams[i]->codec->coder_type == AVMEDIA_TYPE_VIDEO)
	//	{
	//		nVSI = i;
	//		break;
	//	}
	//}

	// 위의 주석된 부분은 channel 0 이 자막일 경우 에러 발생. 아래 코드로 대체
	nVSI = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

	if (nVSI == -1)
	{
		LOG_("codec has not AVMEDIA_TYPE_VIDEO");
		return false;
	}

	pCodecCtx = pFormatCtx->streams[nVSI]->codec;

	AVCodec * pCodec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		LOG_("avcodec_find_decoder error");
		return false; //codec not found
	}

	av_opt_set(pFormatCtx->priv_data, "preset", "ultrafast", 0);
	av_opt_set(pFormatCtx->priv_data, "tune", "zerolatency", 0);

	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		LOG_("avcodec_open2 error");
		return false;
	}

	pFrame = av_frame_alloc();
	pFrameRGB = av_frame_alloc();

	uint8_t *buffer;
	int numBytes;

	AVPixelFormat pFormat = AV_PIX_FMT_BGR24;
	numBytes = avpicture_get_size(pFormat, pCodecCtx->width, pCodecCtx->height);
	buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
	avpicture_fill((AVPicture *)pFrameRGB, buffer, pFormat, pCodecCtx->width, pCodecCtx->height);

	inputWidth = pCodecCtx->width;
	inputHeight = pCodecCtx->height;

	double fps = av_q2d(pFormatCtx->streams[nVSI]->avg_frame_rate);
	if (fps <= 0 || std::isnan(fps))
		g_vidPropInterval = 33;
	else
		g_vidPropInterval = (int)(1000. / fps);

	strFormatNames = pFormatCtx->iformat->name;

	return true;
}

#include <opencv2/opencv.hpp>

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		LOG_("help : argv[1] must be 'video file path' or 'rtsp address'");
		return -1;
	}

	inputVid = argv[1];

	ffmpeg_initialize();
	if (!ffmpeg_open_file())
		return -1;

	cv::Mat imgInput;
	int frameFinished;

	bool bTryReconnect = false;
	bool bRun = true;
	while (bRun)
	{
		if (bTryReconnect)
		{
			if (ffmpeg_open_file())
				bTryReconnect = false;

			Sleep_ms(1000);
			continue;
		}

		int ret = av_read_frame(pFormatCtx, &packet);
		if (ret == AVERROR_EOF)
		{
			LOG_("av_read_frame AVERROR_EOF");

			if (!bTryReconnect)
				ffmpeg_finalize();

			if (strFormatNames != "rtsp")
			{
				bRun = false;
				continue;
			}

			bTryReconnect = true;
			Sleep_ms(1000);
			continue;
		}
		else if (ret < 0)
		{
			continue;
		}

		if (packet.stream_index == nVSI)
		{
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			if (frameFinished)
			{
				struct SwsContext *img_convert_ctx;
				img_convert_ctx = sws_getCachedContext(NULL, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL);
				sws_scale(img_convert_ctx, ((AVPicture*)pFrame)->data, ((AVPicture*)pFrame)->linesize, 0, pCodecCtx->height, ((AVPicture *)pFrameRGB)->data, ((AVPicture *)pFrameRGB)->linesize);

				imgInput = cv::Mat(pFrame->height, pFrame->width, CV_8UC3, pFrameRGB->data[0]).clone();

				av_free_packet(&packet);
				sws_freeContext(img_convert_ctx);
			}
		}
		
		if (frameFinished)
		{
			cv::imshow("imgInput", imgInput);
		}

		int key = cv::waitKey(1);
		switch (key)
		{
		case 27:
			bRun = false;
			break;
		default:
			break;
		}
	}

	LOG_("Exit...");
	return 0;
}
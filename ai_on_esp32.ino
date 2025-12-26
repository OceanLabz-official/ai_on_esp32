/****************************************************
 *  Project   : ESP32 AI Voice Assistant
 *  Board     : ESP32-S3 / ESP32-C3 / ESP32 CLASSIC
 *  Author    : OceanLabz
 *  Version   : 1.0.0
 *  Date      : 2025-02-10
 *
 *  Description:
 *  --------------------------------------------------------
 *  - Records voice using INMP441 I2S microphone
 *  - Converts speech to text using OpenAI Whisper
 *  - Sends text to ChatGPT for AI response
 *  - Converts AI response to speech using ElevenLabs / OpenAI
 *  - Plays MP3 audio via external I2S DAC
 *
 *
 *  YouTube   : https://youtube.com/@OceanLabz
 ****************************************************/
// ==================== INCLUDES ====================
// ````````  
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <ArduinoHttpClient.h>

// Audio libraries for MP3 decoding
#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"


// ===== SELECT TARGET BOARD (uncomment ONE only) =====
// #define BOARD_ESP32_S3
#define BOARD_ESP32
// #define BOARD_ESP32_C3

// ==================== PIN DEFINITIONS ====================

#if defined(BOARD_ESP32_S3)

  // ----- SD Card -----
  #define SD_CS       42
  #define SD_MISO     46
  #define SD_SCLK     2
  #define SD_MOSI     3

  // ----- External DAC (I2S Output) -----
  #define I2S_BCLK    48
  #define I2S_LRC     21
  #define I2S_DOUT    47

  // ----- INMP441 Microphone (I2S Input) -----
  #define I2S_MIC_SERIAL_CLOCK     40
  #define I2S_MIC_LEFT_RIGHT_CLOCK 39
  #define I2S_MIC_SERIAL_DATA      41


#elif defined(BOARD_ESP32)

  // ----- SD Card -----
  #define SD_CS       5
  #define SD_MISO     19
  #define SD_SCLK     18
  #define SD_MOSI     23

  // ----- External DAC (I2S Output) -----
  #define I2S_BCLK    26
  #define I2S_LRC     25
  #define I2S_DOUT    27

  // ----- INMP441 Microphone (I2S Input) -----
  #define I2S_MIC_SERIAL_CLOCK     33
  #define I2S_MIC_LEFT_RIGHT_CLOCK 32
  #define I2S_MIC_SERIAL_DATA      35


#elif defined(BOARD_ESP32_C3)

  // ----- SD Card -----
  #define SD_CS       10
  #define SD_MISO     4
  #define SD_SCLK     6
  #define SD_MOSI     7

  // ----- External DAC (I2S Output) -----
  #define I2S_BCLK    8
  #define I2S_LRC     9
  #define I2S_DOUT    18

  // ----- INMP441 Microphone (I2S Input) -----
  #define I2S_MIC_SERIAL_CLOCK     5
  #define I2S_MIC_LEFT_RIGHT_CLOCK 3
  #define I2S_MIC_SERIAL_DATA      2


#else
    #error " No board selected! Please define BOARD_ESP32_S3, BOARD_ESP32, or BOARD_ESP32_C3"
#endif


// Recording settings
#define RECORDING_DURATION 5            // seconds
#define SAMPLE_RATE 16000               // Hz
#define BUFFER_SIZE 1024

// ==================== CONFIGURATION ====================
// TODO: MOVE THESE TO secrets.h OR USE PREFERENCES/NVS
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// TODO: REMOVE API KEYS FROM CODE - USE ENCRYPTED STORAGE
const char* openaiApiKey = "sk-proj-siqa2E7Q";
const char* elevenLabsApiKey = "sk_137a281";

// API URLs
const char* openaiChatUrl = "https://api.openai.com/v1/chat/completions";
const char* openaiTtsUrl = "https://api.openai.com/v1/audio/speech";
const char* openaiSttUrl = "https://api.openai.com/v1/audio/transcriptions";
const char* elevenLabsTtsUrl = "https://api.elevenlabs.io/v1/text-to-speech/";

const char* voiceId = "21m00Tcm4TlvDq8ikWAM";

// ==================== GLOBALS ====================
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;

i2s_config_t i2s_mic_config;
i2s_pin_config_t i2s_mic_pins;

bool gettingResponse = false;
bool recordingMode = false;

/**
 * @brief Arduino setup function.
 *
 * Initializes Serial communication, SD card, WiFi connection,
 * I2S microphone configuration, and performs microphone diagnostics.
 * Also prints available user commands.
 */

void setup() {
  delay(500);
  Serial.begin(115200);
  delay(500);
  Serial.println("AI on ESP32");
  
  // Initialize SD card
  if (!setupSDCard()) {
    Serial.println("SD Card initialization failed!");
    while(1);
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initialize I2S microphone
  setupI2SMicrophone();
  
  // Run diagnostics
  testMicrophone();
  testMicrophoneDetailed();
  
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  Serial.println("\n=== ESP32 Voice Assistant ===");
  Serial.println("Commands:");
  Serial.println("1. Type text and press Enter for TTS");
  Serial.println("2. Type 'RECORD' to start voice recording and STT");
  Serial.println("3. Type 'TTS:your text' for direct TTS");
  Serial.println("4. Type 'END' to finish text input");
}

/**
 * @brief Main Arduino loop.
 *
 * Handles MP3 audio playback, processes serial commands,
 * and keeps the system responsive.
 */

void loop() {
  // Handle audio playback
  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("Playback finished");
      cleanupAudio();
    }
  }
  
  // Handle serial commands
  handleSerialCommands();
  
  delay(10);
}

/**
 * @brief Handles user commands received via Serial.
 *
 * Supports:
 * - RECORD command for voice input
 * - TTS:text for direct text-to-speech
 * - Plain text input for spoken output
 */

void handleSerialCommands() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.length() > 0) {
      if (command == "RECORD") {
        startRecordingAndTranscription();
      } else if (command.startsWith("TTS:")) {
        String ttsText = command.substring(4);
        ttsText.trim();
        if (ttsText.length() > 0) {
          generateAndPlayAudio(ttsText);
        } else {
          Serial.println("TTS text is empty. Use 'TTS:your text' format");
        }
      } else if (command != "END") {  // Regular text as TTS input
        Serial.print("TTS: ");
        Serial.println(command);
        generateAndPlayAudio(command);
      }
    }
  }
}

/**
 * @brief Generates speech audio from text and plays it.
 *
 * Sends text to the TTS API, saves the MP3 response to SD card,
 * and starts playback using the external I2S DAC.
 *
 * @param text Text to convert into speech
 */

void generateAndPlayAudio(String text) {
  Serial.println("Generating audio...");
  
  if (generateAudio(text)) {
    Serial.println("Audio generated successfully!");
    playAudioFromSD();
  } else {
    Serial.println("Failed to generate audio!");
  }
}

/**
 * @brief Generates MP3 audio using a cloud TTS API.
 *
 * Sends the given text to ElevenLabs (or OpenAI TTS),
 * downloads the generated MP3 audio stream,
 * and stores it on the SD card.
 *
 * @param text Text to convert to speech
 * @return true if audio generation succeeds, false otherwise
 */

bool generateAudio(String text) {
  HTTPClient http;
  
  // NOTE: Currently using ElevenLabs. Switch to OpenAI TTS if preferred
  String url = String(elevenLabsTtsUrl) + voiceId;
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("xi-api-key", elevenLabsApiKey);
  http.addHeader("Accept", "audio/mpeg");
  
  String jsonPayload = "{\"text\":\"" + escapeJsonString(text) + 
                      "\",\"model_id\":\"eleven_multilingual_v2\"," +
                      "\"voice_settings\":{\"stability\":0.5,\"similarity_boost\":0.5}}";
  
  int httpCode = http.POST(jsonPayload);
  


  if (httpCode == HTTP_CODE_OK) {
  int audioSize = http.getSize();
  
    if (audioSize > 0) {
      // Create unique filename
      String filename = "/audio_response.mp3";
      
      // Open file for writing
      File audioFile = SD.open(filename.c_str(), FILE_WRITE);
      if (!audioFile) {
        Serial.println("Failed to open file for writing");
        http.end();
        return false;
      }
      
      // Read audio data and write to file
      WiFiClient* stream = http.getStreamPtr();
      size_t bytesRead = 0;
      uint8_t buffer[1024];
      
      while (http.connected() && bytesRead < (size_t)audioSize) {
        size_t available = stream->available();
        if (available) {
          size_t toRead = min(available, sizeof(buffer));
          size_t read = stream->readBytes(buffer, toRead);
          if (read > 0) {
            audioFile.write(buffer, read);
            bytesRead += read;
          }
        }
        delay(1);
      }
      
      audioFile.close();
      Serial.printf("Audio saved: %s (%d bytes)\n", filename.c_str(), bytesRead);
      
      http.end();
      return true;
    }
  }
  else
  {
    Serial.printf("HTTP Error: %d\n", httpCode);
    Serial.println(http.getString());
  }
  
  http.end();
  return false;
}

/**
 * @brief Plays an MP3 audio file stored on the SD card.
 *
 * Initializes MP3 decoder and I2S output,
 * then streams audio to the external DAC.
 */


void playAudioFromSD() {
  String filename = "/audio_response.mp3";
  
  Serial.printf("Playing: %s\n", filename.c_str());
  
  file = new AudioFileSourceSD(filename.c_str());
  if (!file->isOpen()) {
    Serial.println("Failed to open MP3 file");
    cleanupAudio();
    return;
  }
  
  out = new AudioOutputI2S();
  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetGain(0.9);
  
  mp3 = new AudioGeneratorMP3();
  if (!mp3->begin(file, out)) {
    Serial.println("MP3 decoder begin failed");
    cleanupAudio();
    return;
  }
  
  Serial.println("Playback started");
}

/**
 * @brief Frees all audio-related resources.
 *
 * Stops playback and deletes MP3 decoder,
 * file source, and I2S output objects to prevent memory leaks.
 */

void cleanupAudio() {
  if (mp3) { delete mp3; mp3 = nullptr; }
  if (out) { delete out; out = nullptr; }
  if (file) { delete file; file = nullptr; }
}


/**
 * @brief Records microphone audio and converts speech to text.
 *
 * Records audio from the I2S microphone, saves it as a WAV file,
 * sends it to the Speech-to-Text API, and forwards the result
 * to ChatGPT for processing.
 */

void startRecordingAndTranscription() {
  Serial.println("\n----- Starting Recording -----");
  Serial.println("Please speak... (recording for " + String(RECORDING_DURATION) + " seconds)");

  const char* filename = "/recording.wav";
  
  if (recordAudioToSD(filename)) {
    Serial.println("Recording completed");
    Serial.println("Converting speech to text...");

    String transcribedText = speechToTextHttpClient(filename);
    
    // Optional: Delete recording after processing
    // SD.remove(filename);

    if (transcribedText.length() > 0) {
      Serial.println("\nRecognition result: " + transcribedText);
      Serial.println("\nSending to ChatGPT...");
      chatGptCall(transcribedText);
    } else {
      Serial.println("Failed to recognize text or an error occurred.");
    }
  } else {
    Serial.println("Failed to record audio!");
  }
}

/**
 * @brief Records audio from the INMP441 microphone to SD card.
 *
 * Captures I2S audio, converts it to 16-bit PCM WAV format,
 * applies basic noise gating, and stores the result on SD card.
 *
 * @param filename WAV file path on SD card
 * @return true if recording succeeds, false otherwise
 */

bool recordAudioToSD(const char* filename) {
  if (i2s_driver_install(I2S_NUM_0, &i2s_mic_config, 0, NULL) != ESP_OK) {
    Serial.println("Failed to install I2S driver");
    return false;
  }
  
  if (i2s_set_pin(I2S_NUM_0, &i2s_mic_pins) != ESP_OK) {
    Serial.println("Failed to set I2S pins");
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  
  delay(100);
  
  if (SD.exists(filename)) {
    SD.remove(filename);
  }
  
  File wavFile = SD.open(filename, FILE_WRITE);
  if (!wavFile) {
    Serial.println("Failed to create WAV file");
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  
  uint8_t wavHeader[44];
  uint32_t total_samples = SAMPLE_RATE * RECORDING_DURATION;
  createWavHeader(wavHeader, SAMPLE_RATE, 16, 1, total_samples);
  wavFile.write(wavHeader, 44);
  
  Serial.println("Recording... Speak now!");
  
  const size_t buffer_size = 64;
  int32_t audio_buffer[buffer_size];
  
  uint32_t samples_written = 0;
  unsigned long start_time = millis();
  uint32_t expected_samples = total_samples;
  
  while (samples_written < expected_samples) {
    size_t bytes_read = 0;
    
    esp_err_t result = i2s_read(I2S_NUM_0, 
                               (char*)audio_buffer, 
                               sizeof(audio_buffer), 
                               &bytes_read, 
                               100);
    
    if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
      Serial.println("I2S read error");
      break;
    }
    
    size_t samples_read = bytes_read / sizeof(int32_t);
    
    for (size_t i = 0; i < samples_read && samples_written < expected_samples; i++) {
      int32_t raw_sample = audio_buffer[i];
      int16_t sample_16bit = (int16_t)(raw_sample >> 11);  // Better precision
      
      // Noise gate
      if (abs(sample_16bit) < 200) {
        sample_16bit = 0;
      }
      
      wavFile.write((uint8_t*)&sample_16bit, sizeof(int16_t));
      samples_written++;
    }
    
    if (millis() - start_time > (RECORDING_DURATION * 1000 + 2000)) {
      break;
    }
  }
  
  wavFile.flush();
  
  if (samples_written > 0) {
    wavFile.seek(0);
    updateWavHeader(wavHeader, samples_written);
    wavFile.write(wavHeader, 44);
  }
  
  wavFile.close();
  i2s_driver_uninstall(I2S_NUM_0);
  
  Serial.printf("Recorded: %lu samples, File: %s\n", samples_written, filename);
  return samples_written > 0;
}

/**
 * @brief Converts recorded speech to text using Whisper API.
 *
 * Uploads a WAV audio file via HTTPS multipart request
 * and parses the transcription response.
 *
 * @param filename Path to WAV file on SD card
 * @return Transcribed text (empty string on failure)
 */

String speechToTextHttpClient(const char* filename) {
  String response = "";
  
  File audioFile = SD.open(filename, FILE_READ);
  if (!audioFile) {
    Serial.println("Failed to open audio file");
    return response;
  }
  
  WiFiClientSecure wifiClient;
  wifiClient.setInsecure();
  
  HttpClient client(wifiClient, "api.openai.com", 443);
  client.setHttpResponseTimeout(120000);
  
  String boundary = "----WebKitFormBoundary" + String(millis());
  String contentType = "multipart/form-data; boundary=" + boundary;
  
  size_t fileSize = audioFile.size();
  
  // Calculate content length
  String bodyStart = "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
  bodyStart += "Content-Type: audio/wav\r\n\r\n";
  
  String bodyEnd = "\r\n--" + boundary + "\r\n";
  bodyEnd += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
  bodyEnd += "whisper-1\r\n";
  bodyEnd += "--" + boundary + "--\r\n";
  
  size_t contentLength = bodyStart.length() + fileSize + bodyEnd.length();
  
  Serial.printf("Sending HTTPS request, size: %d bytes\n", contentLength);
  
  client.beginRequest();
  client.post("/v1/audio/transcriptions");
  client.sendHeader("Authorization", "Bearer " + String(openaiApiKey));
  client.sendHeader("Content-Type", contentType);
  client.sendHeader("Content-Length", contentLength);
  client.beginBody();
  
  client.print(bodyStart);
  
  const size_t chunkSize = 512;
  uint8_t buffer[chunkSize];
  size_t totalSent = 0;
  
  while (audioFile.available()) {
    size_t bytesRead = audioFile.read(buffer, chunkSize);
    if (bytesRead > 0) {
      client.write(buffer, bytesRead);
      totalSent += bytesRead;
      
      if (totalSent % 10240 == 0) {
        Serial.printf("Progress: %d/%d bytes (%.1f%%)\n", totalSent, fileSize, (totalSent * 100.0) / fileSize);
      }
    }
    delay(2);
  }
  
  client.print(bodyEnd);
  client.endRequest();
  
  Serial.println("Request sent, waiting for response...");
  
  int httpCode = client.responseStatusCode();
  String httpResponse = client.responseBody();
  
  Serial.print("HTTP Code: ");
  Serial.println(httpCode);
  
  if (httpCode == 200) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, httpResponse);
    
    if (!error && doc.containsKey("text")) {
      response = doc["text"].as<String>();
      Serial.println("Transcription: " + response);
    } else {
      Serial.println("JSON parsing failed");
    }
  } else {
    Serial.print("Error response: ");
    Serial.println(httpResponse);
  }
  
  audioFile.close();
  return response;
}

/**
 * @brief Sends user input to ChatGPT and plays the response.
 *
 * Handles full AI interaction flow:
 * - Sends message to ChatGPT
 * - Prints response
 * - Converts response to speech
 *
 * @param message User input text
 */

void chatGptCall(String message) {
  Serial.println("Sending request to ChatGPT...");
  gettingResponse = true;

  String response = sendMessage(message);
  Serial.println(response);
  
  if (response != "") {
    Serial.print("ChatGPT: ");
    Serial.println(response);

    if (response.length() > 0) {
      Serial.println("Speaking recognized text...");
      generateAndPlayAudio(response);
    }
  } else {
    Serial.println("Failed to get ChatGPT response");
  }
  
  gettingResponse = false;
}

/**
 * @brief Sends a message to the OpenAI Chat Completion API.
 *
 * Builds the request payload, performs HTTP POST,
 * and returns the raw AI response.
 *
 * @param message User input text
 * @return ChatGPT response text
 */

String sendMessage(String message) {
  HTTPClient http;
  http.begin(openaiChatUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(openaiApiKey));

  String payload = buildChatGptPayload(message);
  int httpResponseCode = http.POST(payload);

  if (httpResponseCode == 200) {
    String response = http.getString();
    return processChatGptResponse(response);
  }
  
  Serial.printf("ChatGPT HTTP Error: %d\n", httpResponseCode);
  return "";
}

/**
 * @brief Builds a ChatGPT request JSON payload.
 *
 * Adds system instructions and user message
 * in the required OpenAI format.
 *
 * @param message User input text
 * @return Serialized JSON payload
 */

String buildChatGptPayload(String message) {
  DynamicJsonDocument doc(768);
  doc["model"] = "gpt-4.1-nano";  // Note: This model might not exist
  
  JsonArray messages = doc.createNestedArray("messages");
  
  JsonObject sysMsg = messages.createNestedObject();
  sysMsg["role"] = "system";
  sysMsg["content"] = "Please answer questions briefly, responses should not exceed 30 words.";

  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  userMsg["content"] = message;

  String output;
  serializeJson(doc, output);
  return output;
}

/**
 * @brief Extracts the assistant reply from ChatGPT JSON response.
 *
 * Parses the API response and returns clean text output.
 *
 * @param response Raw JSON response from ChatGPT
 * @return Extracted assistant message
 */

String processChatGptResponse(String response) {
  DynamicJsonDocument jsonDoc(1024);
  DeserializationError error = deserializeJson(jsonDoc, response);
  
  if (!error) {
    String outputText = jsonDoc["choices"][0]["message"]["content"];
    // Remove newlines for cleaner output
    outputText.replace("\n", " ");
    return outputText;
  }
  return "";
}


/**
 * @brief Creates a WAV file header.
 *
 * Generates a standard PCM WAV header based on
 * sample rate, bit depth, channel count, and sample size.
 *
 * @param header Buffer to store WAV header
 * @param sampleRate Audio sample rate
 * @param bitDepth Audio bit depth
 * @param channels Number of audio channels
 * @param numSamples Total number of samples
 */

void createWavHeader(uint8_t* header, uint32_t sampleRate, uint16_t bitDepth, uint16_t channels, uint32_t numSamples) {
  // RIFF header
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  uint32_t fileSize = numSamples * (bitDepth / 8) * channels + 36;
  header[4] = (fileSize) & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  
  // fmt chunk
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0; // PCM format
  header[22] = channels & 0xFF; header[23] = (channels >> 8) & 0xFF;
  header[24] = sampleRate & 0xFF; 
  header[25] = (sampleRate >> 8) & 0xFF;
  header[26] = (sampleRate >> 16) & 0xFF; 
  header[27] = (sampleRate >> 24) & 0xFF;
  
  uint32_t byteRate = sampleRate * channels * (bitDepth / 8);
  header[28] = byteRate & 0xFF; 
  header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF; 
  header[31] = (byteRate >> 24) & 0xFF;
  
  header[32] = channels * (bitDepth / 8);
  header[33] = 0;
  header[34] = bitDepth; header[35] = 0;
  
  // data chunk
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  uint32_t dataSize = numSamples * channels * (bitDepth / 8);
  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}

/**
 * @brief Updates WAV header after recording completes.
 *
 * Fixes file size and data chunk size fields
 * based on actual recorded samples.
 *
 * @param header WAV header buffer
 * @param actual_samples Number of recorded samples
 */

void updateWavHeader(uint8_t* header, uint32_t actual_samples) {
  uint32_t fileSize = actual_samples * 2 + 36;
  header[4] = fileSize & 0xFF;
  header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF;
  header[7] = (fileSize >> 24) & 0xFF;
  
  uint32_t dataSize = actual_samples * 2;
  header[40] = dataSize & 0xFF;
  header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF;
  header[43] = (dataSize >> 24) & 0xFF;
}

/**
 * @brief Configures I2S interface for INMP441 microphone.
 *
 * Sets sample rate, bit depth, channel format,
 * DMA buffers, and I2S pin mapping.
 */

void setupI2SMicrophone() {
  i2s_mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA
  };
  
  Serial.println("I2S Microphone configured for INMP441");
}

/**
 * @brief Performs a basic microphone functionality test.
 *
 * Listens for non-zero audio samples to verify
 * microphone wiring and signal presence.
 */

void testMicrophone() {
  Serial.println("\n=== Testing Microphone ===");
  
  if (i2s_driver_install(I2S_NUM_0, &i2s_mic_config, 0, NULL) != ESP_OK) {
    Serial.println("Failed to install I2S driver");
    return;
  }
  
  if (i2s_set_pin(I2S_NUM_0, &i2s_mic_pins) != ESP_OK) {
    Serial.println("Failed to set I2S pins");
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }
  
  delay(100);
  
  Serial.println("Listening for audio input... Speak into microphone!");
  
  int32_t sample;
  size_t bytes_read;
  unsigned long start = millis();
  int non_zero_count = 0;
  
  while (millis() - start < 3000) {
    if (i2s_read(I2S_NUM_0, (char*)&sample, sizeof(sample), &bytes_read, 100) == ESP_OK) {
      if (bytes_read == sizeof(sample)) {
        int16_t sample_16bit = (int16_t)(sample >> 8);
        if (abs(sample_16bit) > 100) {
          non_zero_count++;
        }
      }
    }
    delay(10);
  }
  
  if (non_zero_count > 0) {
    Serial.printf("Microphone test PASSED: %d audio events detected\n", non_zero_count);
  } else {
    Serial.println("Microphone test FAILED: No audio detected");
  }
  
  i2s_driver_uninstall(I2S_NUM_0);
}

/**
 * @brief Performs an advanced microphone diagnostic test.
 *
 * Reads raw I2S data, measures amplitude levels,
 * detects silent samples, and reports audio quality.
 */

void testMicrophoneDetailed() {
  Serial.println("\n=== Detailed Microphone Test ===");
  
  if (i2s_driver_install(I2S_NUM_0, &i2s_mic_config, 0, NULL) != ESP_OK) {
    Serial.println("Failed to install I2S driver");
    return;
  }
  
  if (i2s_set_pin(I2S_NUM_0, &i2s_mic_pins) != ESP_OK) {
    Serial.println("Failed to set I2S pins");
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }
  
  delay(500);
  
  Serial.println("I2S driver installed successfully");
  Serial.println("Reading raw I2S data for 5 seconds...");
  
  int32_t samples[100];
  size_t bytes_read;
  unsigned long start = millis();
  int total_samples = 0;
  int non_zero_samples = 0;
  int max_amplitude = 0;
  
  while (millis() - start < 5000) {
    if (i2s_read(I2S_NUM_0, (char*)samples, sizeof(samples), &bytes_read, 100) == ESP_OK) {
      int samples_read = bytes_read / sizeof(int32_t);
      total_samples += samples_read;
      
      for (int i = 0; i < samples_read; i++) {
        if (samples[i] != 0) {
          non_zero_samples++;
          int amplitude = abs(samples[i]);
          if (amplitude > max_amplitude) max_amplitude = amplitude;
        }
      }
    }
  }
  
  Serial.println("\n=== Test Results ===");
  Serial.printf("Total samples read: %d\n", total_samples);
  Serial.printf("Non-zero samples: %d\n", non_zero_samples);
  Serial.printf("Max amplitude: %d\n", max_amplitude);
  Serial.printf("Zero sample percentage: %.1f%%\n", 
                (total_samples - non_zero_samples) * 100.0 / total_samples);
  
  if (total_samples == 0) {
    Serial.println("CRITICAL: No samples read from I2S");
  } else if (non_zero_samples == 0) {
    Serial.println("PROBLEM: All samples are zero");
  } else if (max_amplitude < 1000) {
    Serial.println("WARNING: Very low amplitude detected");
  } else {
    Serial.println("SUCCESS: Audio data detected!");
  }
  
  i2s_driver_uninstall(I2S_NUM_0);
}

/**
 * @brief Initializes and mounts the SD card.
 *
 * Configures SPI interface, verifies SD card presence,
 * detects card type, and prints storage information.
 *
 * @return true if SD card is ready, false otherwise
 */

bool setupSDCard() {
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card mount failed!");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  
  return true;
}

/**
 * @brief Connects ESP32 to a WiFi network.
 *
 * Attempts WiFi connection using configured credentials
 * and prints the assigned IP address.
 */

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
    while(1);
  }
}


/**
 * @brief Escapes special characters for JSON compatibility.
 *
 * Converts quotes, newlines, tabs, and backslashes
 * into valid JSON-safe sequences.
 *
 * @param input Raw input string
 * @return Escaped JSON-safe string
 */

String escapeJsonString(String input) {
  input.replace("\\", "\\\\");
  input.replace("\"", "\\\"");
  input.replace("\n", "\\n");
  input.replace("\r", "\\r");
  input.replace("\t", "\\t");
  return input;
}
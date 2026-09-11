#pragma once
typedef struct {
  int steps;
  int sleep;
  int rhr;
  int shr;
  int sleepScore;
  int sleepQuality;
  int hrv;
  int hrvSDNN;
  char date[12];
} WellnessDay;

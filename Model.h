#pragma once
class Model {
public:
  virtual ~Model() {}
  virtual void init() = 0;
  virtual void run() = 0;
};

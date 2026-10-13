#include "application.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace std::chrono_literals;
void Require(bool ok) { if (!ok) throw std::runtime_error("Integration assertion failed"); }
model::Map Map(std::size_t capacity = 3) {
 model::Map m{model::Map::Id{"test"},"Test",4};
 m.SetBagCapacity(capacity);m.SetLootTypesCount(2);
 m.SetLootValue(0,10);m.SetLootValue(1,30);
 m.AddRoad(model::Road{model::Road::HORIZONTAL,{0,0},40});
 return m;
}
int main() {
 try {
  {
   auto map=Map(1);map.AddOffice(model::Office{model::Office::Id{"o"},{6,0},{5,0}});
   model::GameSession s{&map,false,{1s,0.0}};auto& d=s.AddDog("Dog");
   s.AddLostObject(0,{2,0});const auto skipped=s.AddLostObject(1,{4,0}).id;
   const auto last=s.AddLostObject(1,{8,0}).id;
   Require(d.GetScore()==0);d.SetMove(model::Direction::EAST,10);s.Tick(1s);
   Require(d.GetScore()==10 && d.GetBag().size()==1 && d.GetBag()[0].id==last);
   Require(s.GetLostObjects().size()==1 && s.GetLostObjects()[0].id==skipped);
   d.SetMove(model::Direction::WEST,10);s.Tick(500ms);
   Require(d.GetScore()==40 && d.GetBag().empty());
   d.ReturnLoot(map);Require(d.GetScore()==40);
  }
  {
   auto map=Map();model::GameSession s{&map,false,{1s,0.0}};
   auto& slow=s.AddDog("Slow");auto& fast=s.AddDog("Fast");
   slow.SetPosition({4,0});slow.SetMove(model::Direction::EAST,2);
   fast.SetMove(model::Direction::EAST,20);s.AddLostObject(1,{5,0});s.Tick(1s);
   Require(slow.GetBag().empty() && fast.GetBag().size()==1);
   Require(fast.GetScore()==0 && s.GetLostObjects().empty());
  }
  {
   auto map=Map();model::GameSession s{&map,false,{1s,0.0}};auto& d=s.AddDog("Dog");
   const auto far=s.AddLostObject(1,{8,0}).id;const auto near=s.AddLostObject(0,{2,0}).id;
   d.SetMove(model::Direction::EAST,10);s.Tick(1s);
   Require(d.GetBag().size()==2 && d.GetBag()[0].id==near && d.GetBag()[1].id==far);
   Require(d.GetBag()[0].type==0 && d.GetBag()[1].type==1 && d.GetScore()==0);
  }
  {
   auto map=Map();map.AddOffice(model::Office{model::Office::Id{"o"},{6,0},{5,0}});
   model::GameSession s{&map,false,{1s,0.0}};auto& d=s.AddDog("Dog");
   d.SetPosition({0,0.4});Require(d.TryCollect({100,1,{}},3));
   s.AddLostObject(0,{2,0});d.SetMove(model::Direction::EAST,10);s.Tick(1s);
   Require(d.GetScore()==30 && d.GetBag().empty() && s.GetLostObjects().size()==1);
  }
  {
   auto map=Map();model::GameSession s{&map,false,{1s,0.0}};auto& d=s.AddDog("Dog");
   s.AddLostObject(0,{5,0.3});s.AddLostObject(1,{7,0.300001});
   d.SetMove(model::Direction::EAST,10);s.Tick(1s);
   Require(d.GetBag().size()==1 && d.GetBag()[0].type==0);
   Require(s.GetLostObjects().size()==1 && s.GetLostObjects()[0].type==1);
   s.Tick(10s);Require(std::abs(d.GetPosition().x-40.4)<1e-9 && d.GetSpeed().x==0);
  }
  {
   auto map=Map();model::GameSession s{&map,false,{1s,1.0},[]{return 1.0;},42};
   s.AddDog("Dog");s.Tick(1s);Require(s.GetLostObjects().size()==1);
   Require(s.GetLostObjects()[0].type<2);
  }
  {
   auto map=Map();model::Game game;game.AddMap(std::move(map));app::Application app{game};
   const auto join=app.JoinGame(model::Map::Id{"test"},"Dog");
   auto* p=app.FindPlayerByToken(join.token);Require(p!=nullptr);
   p->GetDog().SetPosition({0,0});p->GetDog().SetMove(model::Direction::EAST,4);
   app.Tick(50ms);Require(std::abs(p->GetDog().GetPosition().x-0.2)<1e-9);
  }
  {
   auto map=Map();map.SetLootValue(0,std::numeric_limits<std::uint64_t>::max());
   model::Dog d{model::DogId{0},"Dog",{}};Require(d.TryCollect({0,0,{}},3));d.ReturnLoot(map);
   Require(d.TryCollect({1,1,{}},3));bool overflow=false;
   try{d.ReturnLoot(map);}catch(const std::overflow_error&){overflow=true;}
   Require(overflow && d.GetBag().size()==1 && d.GetScore()==std::numeric_limits<std::uint64_t>::max());
  }
  std::cout << "Movement, collection, return, score: all checks passed\n";
 } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}

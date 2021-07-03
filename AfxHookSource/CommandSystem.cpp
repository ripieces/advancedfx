#include "stdafx.h"

#include "CommandSystem.h"

#include "WrpVEngineClient.h"
#include "RenderView.h"
#include "MirvTime.h"

#include <deps/release/rapidxml/rapidxml.hpp>
#include <deps/release/rapidxml/rapidxml_print.hpp>

#include <shared/StringTools.h>

//TODO: Reduce code duplication.

extern WrpVEngineClient * g_VEngineClient;

CommandSystem g_CommandSystem;

CommandSystem::CommandSystem()
: Enabled(true)
, m_LastTime(-1)
, m_LastTick(-1)
{

}

void CommandSystem::Add(char const * command)
{
	double time = m_LastTime;

	AddAtTime(command, time);
}

void CommandSystem::AddAtTime(char const* command, double time)
{
	if (!IsSupportedByTime())
	{
		Tier0_Warning("Error: Missing hooks for supporting scheduling by time.\n");
		return;
	}

	CCommand* cmd = new CCommand();
	cmd->SetCommand(command);

	Interval interval(time, time, true);
	m_TimeMap.insert({ interval, cmd });
	m_TimeTree = Insert(m_TimeTree, interval, cmd);
}


void CommandSystem::AddTick(char const * command)
{
	double tick = m_LastTick;

	AddAtTick(command, tick);
}

void CommandSystem::AddAtTick(char const* command, double tick)
{
	if (!IsSupportedByTick())
	{
		Tier0_Warning("Error: Missing hooks for supporting scheduling by tick.\n");
		return;
	}

	CCommand* cmd = new CCommand();
	cmd->SetCommand(command);

	Interval interval((double)tick, (double)tick, true);
	m_TickMap.insert({ interval, cmd });
	m_TickTree = Insert(m_TickTree, interval, cmd);
}

void CommandSystem::EditStart(double startTime)
{
	std::multimap<Interval, CCommand*> tmpMap;

	tmpMap.swap(m_TimeMap);

	for (auto it = tmpMap.begin(); it != tmpMap.end(); ++it)
	{
		double time = it->first.Low;

		if (it == tmpMap.begin())
		{
			startTime = startTime - time;
		}

		time += startTime;

		m_TimeMap.insert({ Interval(time, it->first.High + startTime, it->first.Epsilon), it->second});
	}

	DeleteTimeTree();
}

void CommandSystem::EditStartTick(double startTick)
{
	std::multimap<Interval, CCommand*> tmpMap;

	tmpMap.swap(m_TickMap);

	for (auto it = tmpMap.begin(); it != tmpMap.end(); ++it)
	{
		double tick = it->first.Low;

		if (it == tmpMap.begin())
		{
			startTick = startTick - tick;
		}

		tick += startTick;

		m_TickMap.insert({ Interval(tick, it->first.High + startTick, it->first.Epsilon) , it->second });
	}

	DeleteTickTree();
}

bool CommandSystem::Remove(int index)
{
	if (index < (int)m_TickMap.size())
	{
		int idx = 0;

		for (auto it = m_TickMap.begin(); it != m_TickMap.end(); ++it)
		{
			if (idx == index)
			{
				m_TickMap.erase(it);
				DeleteTickTree();
				return true;
			}

			++idx;
		}
	}
	else
	{
		index -= m_TickMap.size();

		int idx = 0;

		for (auto it = m_TimeMap.begin(); it != m_TimeMap.end(); ++it)
		{
			if (idx == index)
			{
				m_TimeMap.erase(it);
				DeleteTimeTree();
				return true;
			}

			++idx;
		}
	}

	return false;
}

void CommandSystem::Clear(void)
{
	for (auto it = m_TickMap.begin(); it != m_TickMap.end(); ++it) delete it->second;
	m_TickMap.clear();
	DeleteTickTree();

	for (auto it = m_TimeMap.begin(); it != m_TimeMap.end(); ++it) delete it->second;
	m_TimeMap.clear();
	DeleteTimeTree();
}

namespace CommandSystemXML {

char * int2xml(rapidxml::xml_document<> & doc, int value)
{
	char szTmp[12];
	_snprintf_s(szTmp, _TRUNCATE, "%i", value);
	return doc.allocate_string(szTmp);
}

char * double2xml(rapidxml::xml_document<> & doc, double value)
{
	char szTmp[196];
	_snprintf_s(szTmp, _TRUNCATE,"%f", value);
	return doc.allocate_string(szTmp);
}

};

using namespace CommandSystemXML;

bool CommandSystem::Save(wchar_t const * fileName)
{
	rapidxml::xml_document<> doc;

	rapidxml::xml_node<> * decl = doc.allocate_node(rapidxml::node_declaration);
	decl->append_attribute(doc.allocate_attribute("version", "1.0"));
	decl->append_attribute(doc.allocate_attribute("encoding", "utf-8"));
	doc.append_node(decl);

	rapidxml::xml_node<> * commandSystem = doc.allocate_node(rapidxml::node_element, "commandSystem");
	doc.append_node(commandSystem);

	rapidxml::xml_node<> * cmds = doc.allocate_node(rapidxml::node_element, "commands");
	commandSystem->append_node(cmds);

	for (auto it = m_TickMap.begin(); it != m_TickMap.end(); ++it)
	{
		rapidxml::xml_node<> * cmd = doc.allocate_node(rapidxml::node_element, "c");
		cmd->append_attribute(doc.allocate_attribute("tick", double2xml(doc, it->first.Low)));

		if (it->first.Low != it->first.High)
		{
			cmd->append_attribute(doc.allocate_attribute("end", double2xml(doc, it->first.High)));
		}

		if (!it->first.Epsilon)
		{
			cmd->append_attribute(doc.allocate_attribute("epsilon", "0"));
		}

		if (it->second->GetFormated())
		{
			cmd->append_attribute(doc.allocate_attribute("formated", "1"));
		}

		size_t numCurves = it->second->GetSize();
		for (size_t idxCurve = 0; idxCurve < numCurves; ++idxCurve)
		{
			rapidxml::xml_node<>* curve = doc.allocate_node(rapidxml::node_element, "curve");

			if (it->second->GetInterp(idxCurve) == CDoubleInterp::Method_Cubic)
				cmd->append_attribute(doc.allocate_attribute("interp", "cubic"));

			for (auto itPt = it->second->GetMap(idxCurve).begin(); itPt != it->second->GetMap(idxCurve).end(); ++itPt)
			{
				rapidxml::xml_node<>* pt = doc.allocate_node(rapidxml::node_element, "p");

				pt->append_attribute(doc.allocate_attribute("t", double2xml(doc, itPt->first)));
				pt->append_attribute(doc.allocate_attribute("v", double2xml(doc, itPt->second)));

				curve->append_node(pt);
			}

			cmd->append_node(curve);
		}

		rapidxml::xml_node<>* body = doc.allocate_node(rapidxml::node_element, "body", it->second->GetCommand());
		cmd->append_node(body);

		cmds->append_node(cmd);
	}

	for(auto it = m_TimeMap.begin(); it != m_TimeMap.end(); ++it)
	{
		rapidxml::xml_node<> * cmd = doc.allocate_node(rapidxml::node_element, "c");
		cmd->append_attribute(doc.allocate_attribute("t", double2xml(doc,it->first.Low)));

		if (it->first.Low != it->first.High)
		{
			cmd->append_attribute(doc.allocate_attribute("end", double2xml(doc, it->first.High)));
		}

		if (it->first.Epsilon)
		{
			cmd->append_attribute(doc.allocate_attribute("epsilon", "1"));
		}

		if (it->second->GetFormated())
		{
			cmd->append_attribute(doc.allocate_attribute("formated", "1"));
		}

		size_t numCurves = it->second->GetSize();
		for (size_t idxCurve = 0; idxCurve < numCurves; ++idxCurve)
		{
			rapidxml::xml_node<>* curve = doc.allocate_node(rapidxml::node_element, "curve");
			
			if(it->second->GetInterp(idxCurve) == CDoubleInterp::Method_Cubic)
				cmd->append_attribute(doc.allocate_attribute("interp", "cubic"));

			for (auto itPt = it->second->GetMap(idxCurve).begin(); itPt != it->second->GetMap(idxCurve).end(); ++itPt)
			{
				rapidxml::xml_node<>* pt = doc.allocate_node(rapidxml::node_element, "p");

				pt->append_attribute(doc.allocate_attribute("t", double2xml(doc, itPt->first)));
				pt->append_attribute(doc.allocate_attribute("v", double2xml(doc, itPt->second)));

				curve->append_node(pt);
			}

			cmd->append_node(curve);
		}

		rapidxml::xml_node<>* body = doc.allocate_node(rapidxml::node_element, "body", it->second->GetCommand());
		cmd->append_node(body);

		cmds->append_node(cmd);
	}

	std::string xmlString;
	rapidxml::print(std::back_inserter(xmlString), doc);

	FILE * pFile = 0;
	_wfopen_s(&pFile, fileName, L"wb");

	if(0 != pFile)
	{
		fputs(xmlString.c_str(), pFile);
		fclose(pFile);
		return true;
	}
	
	return false;
}

bool CommandSystem::Load(wchar_t const * fileName)
{
	bool bOk = false;
	bool bUsedByTick = false;
	bool bUsedByTime = false;

	FILE * pFile = 0;

	_wfopen_s(&pFile, fileName, L"rb");

	if(!pFile)
		return false;
	
	fseek(pFile, 0, SEEK_END);
	size_t fileSize = ftell(pFile);
	rewind(pFile);

	char * pData = new char[fileSize+1];
	pData[fileSize] = 0;

	size_t readSize = fread(pData, sizeof(char), fileSize, pFile);
	bOk = readSize == fileSize;
	if(bOk)
	{
		try
		{
			do
			{
				rapidxml::xml_document<> doc;
				doc.parse<0>(pData);

				rapidxml::xml_node<> * cur_node = doc.first_node("commandSystem");
				if(!cur_node) break;

				// Clear current Map:
				Clear();

				cur_node = cur_node->first_node("commands");
				if(!cur_node) break;

				for(cur_node = cur_node->first_node("c"); cur_node; cur_node = cur_node->next_sibling("c"))
				{
					if (rapidxml::xml_attribute<> * tickAttr = cur_node->first_attribute("tick"))
					{
						double tick = atof(tickAttr->value());

						CCommand* cmd = new CCommand();

						bool epsilon = true;

						if (rapidxml::xml_attribute<>* epsAttr = cur_node->first_attribute("epsilon"))
						{
							if (0 == atoi(epsAttr->value())) epsilon = false;
						}

						Interval range = Interval(tick,tick, epsilon);

						if (NULL == cur_node->first_node("body"))
						{
							cmd->SetCommand(cur_node->value());
						}

						for (rapidxml::xml_node<>* cur_body = cur_node->first_node("body"); cur_body; cur_body = cur_body->next_sibling("body"))
						{
							cmd->SetCommand(cur_body->value());
						}

						if (rapidxml::xml_attribute<>* endTickAttr = cur_node->first_attribute("end"))
						{
							range = Interval(tick, atof(endTickAttr->value()), epsilon);
						}

						if (rapidxml::xml_attribute<>* formatedAttr = cur_node->first_attribute("formated"))
						{
							if (0 != atoi(formatedAttr->value())) cmd->SetFormated(true);
						}

						for (rapidxml::xml_node<>* cur_curve = cur_node->first_node("curve"); cur_curve; cur_curve = cur_curve->next_sibling("curve"))
						{
							cmd->SetSize(cmd->GetSize() + 1);

							if (rapidxml::xml_attribute<>* methodAttr = cur_node->first_attribute("interp"))
							{
								if (0 == strcmp("cubic", methodAttr->value()))
									cmd->SetInterp(cmd->GetSize() - 1, CDoubleInterp::Method_Cubic);
							}

							for (rapidxml::xml_node<>* cur_point = cur_curve->first_node("p"); cur_point; cur_point = cur_point->next_sibling("p"))
							{
								rapidxml::xml_attribute<>* pointTimeAttr = cur_point->first_attribute("t");
								rapidxml::xml_attribute<>* pointValueAttr = cur_point->first_attribute("v");

								if (pointTimeAttr && pointValueAttr)
								{
									cmd->GetMap(cmd->GetSize() - 1).insert({atof(pointTimeAttr->value()), atof(pointValueAttr->value())});
								}
							}
						}

						bUsedByTick = true;
						m_TickMap.insert({ range, cmd });
						m_TickTree = Insert(m_TickTree, range, cmd);
					}

					if (rapidxml::xml_attribute<> * timeAttr = cur_node->first_attribute("t"))
					{
						double time = atof(timeAttr->value());

						CCommand* cmd = new CCommand();

						bool epsilon = true;

						if (rapidxml::xml_attribute<>* epsAttr = cur_node->first_attribute("epsilon"))
						{
							if (0 == atoi(epsAttr->value())) epsilon = false;
						}
						
						Interval range = Interval(time,time, epsilon);

						if (NULL == cur_node->first_node("body"))
						{
							cmd->SetCommand(cur_node->value());
						}

						for (rapidxml::xml_node<>* cur_body = cur_node->first_node("body"); cur_body; cur_body = cur_body->next_sibling("body"))
						{
							cmd->SetCommand(cur_body->value());
						}

						if (rapidxml::xml_attribute<>* endTimeAttr = cur_node->first_attribute("end"))
						{
							range = Interval(time, atof(endTimeAttr->value()), epsilon);
						}

						if (rapidxml::xml_attribute<>* formatedAttr = cur_node->first_attribute("formated"))
						{
							if (0 != atoi(formatedAttr->value())) cmd->SetFormated(true);
						}

						for (rapidxml::xml_node<>* cur_curve = cur_node->first_node("curve"); cur_curve; cur_curve = cur_curve->next_sibling("curve"))
						{
							cmd->SetSize(cmd->GetSize() + 1);

							if (rapidxml::xml_attribute<>* methodAttr = cur_node->first_attribute("interp"))
							{
								if (0 == strcmp("cubic", methodAttr->value()))
									cmd->SetInterp(cmd->GetSize() - 1, CDoubleInterp::Method_Cubic);
							}

							for (rapidxml::xml_node<>* cur_point = cur_curve->first_node("p"); cur_point; cur_point = cur_point->next_sibling("p"))
							{
								rapidxml::xml_attribute<>* pointTimeAttr = cur_point->first_attribute("t");
								rapidxml::xml_attribute<>* pointValueAttr = cur_point->first_attribute("v");

								if (pointTimeAttr && pointValueAttr)
								{
									cmd->GetMap(cmd->GetSize() - 1).insert({ atof(pointTimeAttr->value()), atof(pointValueAttr->value()) });
								}
							}
						}

						bUsedByTime = true;
						m_TimeMap.insert({ range, cmd });
						m_TimeTree = Insert(m_TimeTree, range, cmd);
					}
				}
			}
			while (false);
		}
		catch(rapidxml::parse_error &)
		{
			bOk=false;
		}
	}

	delete[] pData;

	fclose(pFile);

	if (bUsedByTick && !IsSupportedByTick())
	{
		Tier0_Warning("Error: Missing hooks for supporting scheduling by tick.\n");
	}
	if (bUsedByTick && !IsSupportedByTime())
	{
		Tier0_Warning("Error: Missing hooks for supporting scheduling by time.\n");
	}

	return bOk;
}

void CommandSystem::Console_List(void)
{
	int idx = 0;

	Tier0_Msg("index: tick -> command\n");

	for (auto it = m_TickMap.begin(); it != m_TickMap.end(); ++it)
	{
		if (it->first.Low == it->first.High)
		{
			Tier0_Msg("%i: %f (eps=%i) -> %s\n",
				idx,
				it->first.Low,
				it->first.Epsilon,
				it->second->GetCommand()
			);
		}
		else
		{
			Tier0_Msg("%i: %f - %f (eps=%i) -> %s\n",
				idx,
				it->first.Low,
				it->first.High,
				it->first.Epsilon,
				it->second->GetCommand());
		}

		++idx;
	}

	Tier0_Msg("----\n");

	Tier0_Msg("index: time -> command\n");

	for (auto it = m_TimeMap.begin(); it != m_TimeMap.end(); ++it)
	{
		if (it->first.Low == it->first.High)
		{
			Tier0_Msg("%i: %f (eps=%i) -> %s\n",
				idx,
				it->first.Low,
				it->first.Epsilon,
				it->second->GetCommand()
			);
		}
		else
		{
			Tier0_Msg("%i: %f - %f (eps=%i) -> %s\n",
				idx,
				it->first.Low,
				it->first.High,
				it->first.Epsilon,
				it->second->GetCommand());
		}

		++idx;
	}

	Tier0_Msg("----\n");
}

void CommandSystem::Do_Commands(void)
{
	if (IsSupportedByTick())
	{
		double tick = (double)g_VEngineClient->GetDemoInfoEx()->GetDemoPlaybackTick() + (double)g_Hook_VClient_RenderView.GetGlobals()->interpolation_amount_get();

		//Tier0_Msg("%i\n", tick);

		if (0 != tick) // this can happen when using the prev-tick button on demoui
		{
			if (Enabled && 0 < m_TickMap.size() && m_LastTick != tick)
			{
				EnsureTickTree();

				OverlapExecute(m_TickTree, Interval(m_LastTick, (double)tick , false));
			}

			m_LastTick = tick;
		}
	}

	if (IsSupportedByTime())
	{
		double time = g_MirvTime.GetTime();

		//Tier0_Msg("%f\n", time);

		if (Enabled && 0 < m_TimeMap.size() && m_LastTime != time)
		{
			EnsureTimeTree();

			OverlapExecute(m_TimeTree, Interval(m_LastTime, time, false));
		}

		m_LastTime = time;
	}
}

void CommandSystem::OnLevelInitPreEntityAllTools(void)
{
	m_LastTime = -1;
	m_LastTick = -1;
}

bool CommandSystem::IsSupportedByTime(void)
{
	return 0 != g_VEngineClient && 0 != g_Hook_VClient_RenderView.GetGlobals();
}

bool CommandSystem::IsSupportedByTick(void)
{
	return 0 != g_VEngineClient && 0 != g_Hook_VClient_RenderView.GetGlobals() && 0 != g_VEngineClient->GetDemoInfoEx();
}


bool CommandSystem::CCommand::DoCommand(double t01)
{
	if (!m_Formated)
	{
		g_VEngineClient->ClientCmd_Unrestricted(m_Command.c_str()); // We don't use ExecuteCliendCmd here, because people might clear the commands while they are executed, which we would need to track otherwise in order to prevent crashes, should be accurate enough hopefully.
		return true;
	}
	
	std::string fCommand;

	bool inBegin = false;
	bool inEnd = false;
	std::string::const_iterator itBegin;
	std::string::const_iterator itEnd;
	std::string strIdx;
	int idx = 0;
	double val = 0;
	char pszValue[100];
	pszValue[0] = 0;

	for (auto it = m_Command.cbegin(); it != m_Command.cend(); ++it)
	{
		if (inBegin)
		{
			switch (*it)
			{
			case '{':
				inBegin = false;
				fCommand += '{';
				break;
			case '}':
				inBegin = false;
				strIdx.assign(itBegin, itEnd);
				idx = atoi(strIdx.c_str());
				if (idx < 0 || idx >= (int)m_Interp.size()) return false;
				if (!m_Interp[idx].CanEval()) return false;
				val = m_Interp[idx].Eval(t01);
				_snprintf_s(pszValue, _TRUNCATE, "%f", val);
				fCommand += pszValue;
				break;
			default:
				++itEnd;
			}
		}
		else if(inEnd)
		{
			switch (*it)
			{
			case '}':
				inEnd = false;
				fCommand += '}';
				break;
			default:
				return false;
			}
		}
		else
		{
			switch (*it)
			{
			case '{':
				inBegin = true;
				itBegin = it + 1;
				itEnd = itBegin;
				break;
			case '}':
				inEnd = true;
				break;
			default:
				fCommand += *it;
			}
		}
	}

	g_VEngineClient->ClientCmd_Unrestricted(fCommand.c_str());

	return true;
}

void CommandSystem::AddCurves(IWrpCommandArgs* args)
{
	const char* arg0 = args->ArgV(0);
	int argC = args->ArgC();

	if (5 <= argC)
	{
		CCommand* cmd = new CCommand();

		cmd->SetFormated(true);

		int idx = 4;
		int dim = 0;

		bool tick = true;

		double begin = atof(args->ArgV(2));
		double end = atof(args->ArgV(3));

		if (0 == _stricmp("tick", args->ArgV(1)))
		{
		}
		else if (0 == _stricmp("time", args->ArgV(1)))
		{
			tick = false;
		}
		else {
			Tier0_Warning("AFXERRORR: Expected time or tick.\n");
			delete cmd;
			return;
		}

		double d = end - begin;
		if (d) d = 1 / d;

		while (idx < argC && 0 == strcmp("-", args->ArgV(idx)))
		{
			cmd->SetSize(dim + 1);

			++idx;

			bool abs = false;

			while (true)
			{
				if (StringIBeginsWith(args->ArgV(idx), "interp="))
				{
					cmd->SetInterp(dim, 0 == _stricmp("cubic", args->ArgV(idx) + strlen("interp=")) ? CDoubleInterp::Method_Cubic : CDoubleInterp::Method_Linear);
				}
				else if (StringIBeginsWith(args->ArgV(idx), "space="))
				{
					abs = 0 == _stricmp("abs", args->ArgV(idx) + strlen("space="));
				}
				else break;

				++idx;
			}

			int breakPoint = idx;

			while (breakPoint < argC && 0 != strcmp("-", args->ArgV(breakPoint)) && 0 != strcmp("--", args->ArgV(breakPoint)))
				++breakPoint;

			if ((breakPoint - idx) & 0x1)
			{
				Tier0_Warning("AFXERRORR: CurveTime-Value pairs are not balanced (are odd).\n");
				delete cmd;
				return;
			}

			int ptArgs = (breakPoint - idx) / 2;

			for (int i = 0; i < ptArgs; ++i)
			{
				double t = atof(args->ArgV(idx + i * 2));

				if (abs) t = (t - begin) * d;

				cmd->GetMap(dim).insert({ t, atof(args->ArgV(idx + i * 2 + 1)) });
			}

			++dim;
			idx = breakPoint;
		}

		if(argC <= idx || 0 != strcmp("--", args->ArgV(idx)))
		{
			Tier0_Warning("AFXERRORR: Commands separator \"--\" not found.\n");
			delete cmd;
			return;
		}

		++idx;

		std::string cmdStr = "";

		while (idx < argC)
		{
			if(!cmdStr.empty()) cmdStr += " ";
			cmdStr += args->ArgV(idx);

			++idx;
		}

		cmd->SetCommand(cmdStr.c_str());

		if (tick)
		{
			Interval interval(begin,end, false);
			m_TickMap.insert({ interval, cmd });
			m_TickTree = Insert(m_TickTree, interval, cmd);
		}
		else
		{
			Interval interval(begin, end, false);
			m_TimeMap.insert({interval, cmd });
			m_TimeTree = Insert(m_TimeTree, interval, cmd);
		}
		return;
	}

	Tier0_Msg(
		"%s tick <iStartTick> <iEndTick> [\"-\" [interp=(linear|cubic)] [space=rel|abs] <fCurveTime1> <fCurveValue1> ... <fCurveTimeN> <fCurveValueN>]* \"--\" <formatedCommand1> ... <formatedCommandM> - adds a curve based on ticks, curveTime is in [0,1]. \n"
		"%s time <fStartTime> <iEndTime> [\"-\" [interp=(linear|cubic)] [space=rel|abs] <fCurveTime1> <fCurveValue1> ... <fCurveTimeN> <fCurveValueN>]* \"--\" <formatedCommand1> ... <formatedCommandM> - adds a curve based on time, curveTime is in [0,1]. \n"
		, arg0
		, arg0
	);
}

void CommandSystem::EditCommand(IWrpCommandArgs* args)
{
	const char* arg0 = args->ArgV(0);
	int argC = args->ArgC();

	if (3 <= argC)
	{
		int cmdIndex = atoi(args->ArgV(1));

		CCommand* cmd = nullptr;
		auto itTick = m_TickMap.end();
		auto itTime = m_TimeMap.end();
		Interval interval;

		if (cmdIndex < (int)m_TickMap.size())
		{
			int idx = 0;

			for (auto it = m_TickMap.begin(); it != m_TickMap.end(); ++it)
			{
				if (idx == cmdIndex)
				{
					itTick = it;
					interval = it->first;
					cmd = it->second;
					break;
				}

				++idx;
			}
		}
		else
		{
			cmdIndex -= m_TickMap.size();

			int idx = 0;

			for (auto it = m_TimeMap.begin(); it != m_TimeMap.end(); ++it)
			{
				if (idx == cmdIndex)
				{
					itTime = it;
					interval = it->first;
					cmd = it->second;
					break;
				}

				++idx;
			}
		}

		if (nullptr == cmd)
		{
			Tier0_Warning("AFXERROR: Invalid command index.\n");
			return;
		}

		const char* arg2 = args->ArgV(2);

		if (0 == _stricmp("start", arg2))
		{
			if (4 <= argC)
			{
				double oldDiff = interval.High - interval.Low;
				bool bNoAuto = false;

				if (5 <= argC && 0 == _stricmp("noAuto", args->ArgV(3)))
					bNoAuto = true;

				interval.Low = atof(bNoAuto ? args->ArgV(4) : args->ArgV(3));

				if (!bNoAuto)
				{
					interval.High = interval.Low + oldDiff;
				}


				if (itTick != m_TickMap.end())
				{
					DeleteTickTree();
					m_TickMap.erase(itTick);
					m_TickMap.insert({ interval, cmd });
				}
				if (itTime != m_TimeMap.end())
				{
					DeleteTickTree();
					m_TimeMap.erase(itTime);
					m_TimeMap.insert({ interval, cmd });
				}
				return;
			}

			Tier0_Msg(
				"%s start [noAuto] <fValue> -  Set start tick / time, if noAuto option is given, then the end is not automatically shifted as well.\n"
				"Current value: %i\n"
				, arg0
				, interval.Low ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("end", arg2))
		{
			if (4 <= argC)
			{
				double oldDiff = interval.Low - interval.High;
				double oldVal = interval.High;
				bool bNoAuto = false;

				if (5 <= argC && 0 == _stricmp("noAuto", args->ArgV(3)))
					bNoAuto = true;

				interval.High = atof(bNoAuto ? args->ArgV(4) : args->ArgV(3));

				if (!bNoAuto)
				{
					interval.Low = interval.High + oldDiff;
				}

				if (itTick != m_TickMap.end())
				{
					DeleteTickTree();
					m_TickMap.erase(itTick);
					m_TickMap.insert({ interval, cmd });
				}
				if (itTime != m_TimeMap.end())
				{
					DeleteTickTree();
					m_TimeMap.erase(itTime);
					m_TimeMap.insert({ interval, cmd });
				}
				return;
			}

			Tier0_Msg(
				"%s end [noAuto] <fValue> -  Set start tick / time, if noAuto option is given, then the start is not automatically shifted as well.\n"
				"Current value: %i\n"
				, arg0
				, interval.High ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("startEnd", arg2))
		{

		}
		else if (0 == _stricmp("epsilon", arg2))
		{
			if (4 <= argC)
			{
				interval.Epsilon = 0 != atoi(args->ArgV(3));
				if (itTick != m_TickMap.end())
				{
					DeleteTickTree();
					m_TickMap.erase(itTick);
					m_TickMap.insert({ interval, cmd });
				}
				if (itTime != m_TimeMap.end())
				{
					DeleteTickTree();
					m_TimeMap.erase(itTime);
					m_TimeMap.insert({ interval, cmd });
				}
				return;
			}

			Tier0_Msg(
				"%s epsilon 0|1 -  Set if command string is formated (1) (usually e.g. for curve values) or not (0).\n"
				"Current value: %i\n"
				, arg0
				, interval.Epsilon ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("cmd", arg2))
		{
			if (4 <= argC)
			{
				std::string command = "";

				for (int i = 3; i < argC; ++i)
				{
					if (3 < i) command += " ";
					command += args->ArgV(i);
				}

				cmd->SetCommand(command.c_str());
				return;
			}

			Tier0_Msg(
				"%s cmd <sSommand1> ... <sSommandN> - Set if to use open (0) or closed interval (1) end.\n"
				"Current value: %s\n"
				, arg0
				, cmd->GetCommand()
			);
			return;
		}
		else if (0 == _stricmp("formated", arg2))
		{
			if (4 <= argC)
			{
				cmd->SetFormated(0 != atoi(args->ArgV(3)));
				return;
			}

			Tier0_Msg(
				"%s formated 0|1 -  Set if command string is formated (1) (usually e.g. for curve values) or not (0}.\n"
				"Current value: %i\n"
				, arg0
				, cmd->GetFormated() ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("curves", arg2))
		{
			CSubWrpCommandArgs subArgs(args, 3);
			EditCommandCurves(interval, cmd, &subArgs);
			return;
		}
	}

	Tier0_Msg(
		"%s <index> start [...] - Edit start tick / time.\n"
		"%s <index> end [...] - Edit end tick / time.\n"
		"%s <index> epsilon [...] - Edit if to use open or closed interval end.\n"
		"%s <index> cmd [...] - Edit command string.\n"
		"%s <index> formated [...] - Set if command string is formated (usually e.g. for curve values).\n"
		"%s <index> curves [...] - Edit curves attached to command.\n"
		"Be aware: The index can and often will change when editing start/end!\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

void CommandSystem::EditCommandCurves(Interval I, CCommand* c, IWrpCommandArgs* args)
{
	const char* arg0 = args->ArgV(0);
	int argC = args->ArgC();

	if (2 <= argC)
	{
		const char* arg1 = args->ArgV(1);

		if (0 == _stricmp("add", arg1) && 3 <= argC)
		{
			int curveIndex = atoi(args->ArgV(2));
			if (-1 == curveIndex) curveIndex = (int)c->GetSize();
			if (curveIndex < 0 || (int)c->GetSize() < curveIndex)
			{
				Tier0_Warning("Invalid curve index.\n");
				return;
			}

			c->Add(curveIndex);
			return;
		}
		else if (0 == _stricmp("edit", arg1) && 3 <= argC)
		{
			int curveIndex = atoi(args->ArgV(2));
			if (curveIndex < 0 || (int)c->GetSize() <= curveIndex)
			{
				Tier0_Warning("Invalid curve index.\n");
				return;
			}
			if (4 <= argC)
			{
				const char* curveCmd = args->ArgV(3);

				if (0 == _stricmp("add", curveCmd) && 6 <= argC)
				{
					bool abs = 7 <= argC && 0 == _stricmp("abs", args->ArgV(4));

					double dKey = atof(args->ArgV(abs ? 5 : 4));
					double dValue = atof(args->ArgV(abs ? 6 : 5));

					if (abs)
					{
						double d = I.High - I.Low;
						if (d) d = 1.0 / d;

						dKey = (dKey - I.Low) * d;
					}

					c->GetMap(curveIndex)[dKey] = dValue;
					c->TriggerMapChanged(curveIndex);

					return;
				}
				else if (0 == _stricmp("edit", curveCmd) && 6 <= argC)
				{
					int tidx = atoi(args->ArgV(4));
					int cidx = 0;

					for (auto it = c->GetMap(curveIndex).begin(); it != c->GetMap(curveIndex).end(); ++it)
					{
						if (cidx == tidx)
						{
							it->second = atof(args->ArgV(5));
							c->TriggerMapChanged(curveIndex);
							return;
						}
						++cidx;
					}

					Tier0_Warning("Invalid key/value index.\n");
					return;
				}
				else if (0 == _stricmp("remove", curveCmd) && 5 <= argC)
				{
					int tidx = atoi(args->ArgV(4));
					int cidx = 0;

					for (auto it = c->GetMap(curveIndex).begin(); it != c->GetMap(curveIndex).end(); ++it)
					{
						if (cidx == tidx)
						{
							c->GetMap(curveIndex).erase(it);
							c->TriggerMapChanged(curveIndex);
							return;
						}
						++cidx;
					}

					Tier0_Warning("Invalid key/value index.\n");
					return;
				}
				else if (0 == _stricmp("clear", curveCmd) && 4 <= argC)
				{
					c->GetMap(curveIndex).clear();
					c->TriggerMapChanged(curveIndex);
					return;
				}
				else if (0 == _stricmp("print", curveCmd) && 4 <= argC)
				{
					int idx = 0;
					for (auto it = c->GetMap(curveIndex).begin(); it != c->GetMap(curveIndex).end(); ++it)
					{
						double key = it->first;
						double value = it->second;
						double d = I.High - I.Low;
						double absKey = key * d + I.Low;

						Tier0_Msg(
							"%i: %f (%f) -> %f\n",
							idx,
							key,
							absKey,
							value
						);

						++idx;
					}
					
					return;
				}
			}

			Tier0_Msg(
				"%s edit %i add [abs] <fKey> <fValue> - Insert value at key (overwriting existing values), use \"abs\" for inserting in absolute tick / time space of the command.\n"
				"%s edit %i edit <iIndex> <fValue> - Edit value position <iIndex>.\n"
				"%s edit %i remove <iIndex> - Remove at position <iIndex>.\n"
				"%s edit %i clear - Remove all keys.\n"
				"%s edit %i print - Print values.\n"
				, arg0, curveIndex
				, arg0, curveIndex
				, arg0, curveIndex
				, arg0, curveIndex
				, arg0, curveIndex
			);
			return;
		}
		else if (0 == _stricmp("remove", arg1) && 3 <= argC)
		{
			int curveIndex = atoi(args->ArgV(2));
			if (curveIndex < 0 || (int)c->GetSize() <= curveIndex)
			{
				Tier0_Warning("Invalid curve index.\n");
				return;
			}
			
			c->Remove(curveIndex);
		}
		else if (0 == _stricmp("interp", arg1) && 4 <= argC)
		{
			int curveIndex = atoi(args->ArgV(2));
			if (curveIndex < 0 || (int)c->GetSize() <= curveIndex)
			{
				Tier0_Warning("Invalid curve index.\n");
				return;
			}

			c->SetInterp(curveIndex, 0 == _stricmp("cubic", args->ArgV(3)) ? CDoubleInterp::Method_Cubic : CDoubleInterp::Method_Linear);

			return;
		}
		else if (0 == _stricmp("clear", arg1))
		{
			c->ClearCurves();
			return;
		}
		else if (0 == _stricmp("print", arg1))
		{
			Tier0_Msg("Curves:\n");
			for (int i = 0; i < (int)c->GetSize(); ++i)
			{
				Tier0_Msg("%i: interp=%s size=%i\n"
					, i
					, c->GetInterp(i) == CDoubleInterp::Method_Linear ? "linear" : "cubic"
					, c->GetMap(i).size()
				);
			}

			return;
		}
	}

	Tier0_Msg(
		"%s add <iIndex> - Add a curve at position <iIndex> (use -1 for inserting at the end).\n"
		"%s edit <iIndex> [...] - Edit curve at position <iIndex>.\n"
		"%s remove <iIndex> - Remove curve at position <iIndex>.\n"
		"%s interp <iIndex> linear|cubic - Edit interpolation of curve at position <iIndex>.\n"
		"%s clear - Remove all curves.\n"
		"%s print - Print curves briefly.\n"
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
		, arg0
	);
}

#pragma once

namespace SG
{

	struct IProcess
	{
		virtual ~IProcess() = default;

		virtual void OnInit() = 0;
		virtual void OnUpdate() = 0;
		virtual void OnExit() = 0;
	};

}
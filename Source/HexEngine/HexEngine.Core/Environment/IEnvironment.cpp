
#include "IEnvironment.hpp"

namespace HexEngine
{
	HEX_API IEnvironment* g_pEnv = nullptr;

	void IEnvironment::DestroyEnvironment(IEnvironment* environment)
	{
		if (!environment)
			return;		

		environment->Destroy();
	}

	void HEX_API DestroyEnvironment()
	{
		IEnvironment::DestroyEnvironment(g_pEnv);
	}

	void IEnvironment::AddGameExtension(IGameExtension* extension)
	{
		_gameExtensions.push_back(extension);
	}

	void IEnvironment::RemoveGameExtension(IGameExtension* extension)
	{
		_gameExtensions.erase(std::remove(_gameExtensions.begin(), _gameExtensions.end(), extension), _gameExtensions.end());
	}
}
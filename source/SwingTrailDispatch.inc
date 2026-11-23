void PrecisionHandler::UpdateRedsandSwingTrails(float delta, bool afterCamera)
{
	const auto now = *g_durationOfApplicationRunTimeMS;
	if (afterCamera || !redsandCameraFrame) { redsandCameraFrame = now; }
	if ((!afterCamera && now - *redsandCameraFrame < 100u) ||
		redsandSwingFrame == now || RE::UI::GetSingleton()->GameIsPaused()) { return; }
	redsandSwingFrame = now;
	if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->Get3D()) {
		AttackTrail::WarmRedsandSwingModel();
	}
	std::erase_if(_attackTrails, [&](const auto& trail) {
		return trail->IsRedsandSwingTrail() && !trail->Update(delta);
	});
}

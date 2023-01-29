#include "Kanan.hpp"
#include "Addr.h"

#include "Log.hpp"

namespace kanan {
	static LPBYTE DllAddresses[DllAddress_size];

	LPBYTE CAddr::Addr(DllAddress addrNumber)
	{
		if (DllAddresses[addrNumber] != NULL)
			return DllAddresses[addrNumber];
		if (CacheAddress(addrNumber))
			return DllAddresses[addrNumber];
		return NULL;
	}

	bool CAddr::PreCacheAddresses(void)
	{
		bool result = true;
		for (int i = 0; i < DllAddress_size; i++)
			result &= CacheAddress((DllAddress)i);

		if (result)
			log("Caching of addresses successful.\n");
		else
			log("Warning: failed to cache some addresses.\n");

		return result;
	}

	bool CAddr::CacheAddress(DllAddress addrNumber)
	{
		wchar_t *dllfile;
		char *dllfunction;
		char *fullname;

		switch (addrNumber) {

			// kernel32.dll

		case kernel32_CreateRemoteThread:
			dllfile = L"kernel32.dll";
			fullname = "CRT";
			dllfunction = "CreateRemoteThread";
			break;

			// kernelbase.dll

		case kernelbase_CreateRemoteThreadEx:
			dllfile = L"KERNELBASE.dll";
			fullname = "CRTX";
			dllfunction = "CreateRemoteThreadEx";
			break;

			// esl.dll

		case CStringTUni_Constr:
			dllfile = L"esl.dll";
			fullname = "esl::CStringT::CStringT(void)";
			dllfunction = "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ";
			break;
		case CStringTUni_Equals:
			dllfile = L"esl.dll";
			fullname = "esl::CStringT::operator=(wchar_t *)";
			dllfunction = "??4?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAEAAV01@PB_W@Z";
			break;
		case CFileSystem_SetLookUpOrder:
			dllfile = L"esl.dll";
			fullname = "esl::CFileSystem::SetLookUpOrder";
			dllfunction = "?SetLookUpOrder@CFileSystem@esl@@QAEXW4EFileLookUpOrder@esl_constant@2@@Z";
			break;
		case CFormatter_Insert_ulong:
			dllfile = L"esl.dll";
			fullname = "esl::CFormatter::operator<<(ulong)";
			dllfunction = "??6CFormatter@esl@@QAEAAV01@K@Z";
			break;
		case CFormatter_Insert_ushort:
			dllfile = L"esl.dll";
			fullname = "esl::CFormatter::operator<<(ushort)";
			dllfunction = "??6CFormatter@esl@@QAEAAV01@G@Z";
			break;
		case CFormatter_Insert_float:
			dllfile = L"esl.dll";
			fullname = "esl::CFormatter::operator<<(float)";
			dllfunction = "??6CFormatter@esl@@QAEAAV01@M@Z";
			break;
		case CFormatter_Insert_pwchar:
			dllfile = L"esl.dll";
			fullname = "esl::CFormatter::operator<<(wchar_t const *)";
			dllfunction = "??6CFormatter@esl@@QAEAAV01@PB_W@Z";
			break;
		case win32_CreateWindowExW:
			dllfile = L"esl.dll";
			fullname = "esl::win32::CreateWindowExW(unsigned long,wchar_t const *,wchar_t const *,unsigned long,int,int,int,int,struct HWND__ *,struct HMENU__ *,struct HINSTANCE__ *,void *)";
			dllfunction = "?CreateWindowExW@win32@esl@@YAPAUHWND__@@KPB_W0KHHHHPAU3@PAUHMENU__@@PAUHINSTANCE__@@PAX@Z";
			break;
		case CStringT_Implement:
			dllfile = L"esl.dll";
			fullname = "esl::CStringT<wchar_t,esl::unicode_string_trait,esl::unicode_string_implement>::CStringT<wchar_t,esl::unicode_string_trait,esl::unicode_string_implement>(wchar_t *Src)";
			dllfunction = "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z";
			break;

			// pleione.dll

		case CGameOptionMgr_ReadFSAA:
			dllfile = L"pleione.dll";
			fullname = "pleione::CGameOptionMgr::ReadFSAA";
			dllfunction = "?ReadFSAA@CGameOptionMgr@pleione@@QAE?AW4EFSAA@pleione_constant@2@XZ";
			break;
		case CGameOptionMgr_ReadFSAAQuality:
			dllfile = L"pleione.dll";
			fullname = "pleione::CGameOptionMgr::ReadFSAAQuality";
			dllfunction = "?ReadFSAAQuality@CGameOptionMgr@pleione@@QAEKXZ";
			break;
		case CPleione_CleanUp:
			dllfile = L"pleione.dll";
			fullname = "pleione::CPleione::CleanUp";
			dllfunction = "?CleanUp@CPleione@pleione@@QAE_NXZ";
			break;

			// renderer2.dll

		case CAtmosphere_SetSkyTime:
			dllfile = L"renderer2.dll";
			fullname = "pleione::CAtmosphere::SetSkyTime";
			dllfunction = "?SetSkyTime@CAtmosphere@pleione@@QAEXM@Z";
			break;
		case CQuaterViewCamera_CQuaterViewCamera:
			dllfile = L"renderer2.dll";
			fullname = "pleione::CQuaterViewCamera::CQuaterViewCamera";
			dllfunction = "??0CQuaterViewCamera@pleione@@QAE@XZ";
			break;
		case CDefaultCompiler_Compile:
			dllfile = L"Renderer2.dll";
			fullname = "pleione::CDefaultCompiler::Compile";
			dllfunction = "?Compile@CDefaultCompiler@pleione@@UAEPAVCCompiledText@2@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@ABUtext_option@2@@Z";
			break;
		case CDefaultTagCompiler_Compile:
			dllfile = L"Renderer2.dll";
			fullname = "pleione::CDefaultTagCompiler::Compile";
			dllfunction = "?Compile@CDefaultTagCompiler@pleione@@UAEPAVCCompiledText@2@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@ABUtext_option@2@@Z";
			break;
		case CHTMLCompiler_Compile:
			dllfile = L"Renderer2.dll";
			fullname = "pleione::CHTMLCompiler::Compile";
			dllfunction = "?Compile@CHTMLCompiler@pleione@@UAEPAVCCompiledText@2@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@ABUtext_option@2@@Z";
			break;
		case Camera_SetProjectionDesc:
			dllfile = L"Renderer2.dll";
			fullname = "pleione::camera::SetProjectionDesc(pleione::camera *this, float, float, float)";
			dllfunction = "?SetProjectionDesc@camera@pleione@@QAEXMMM@Z";
			break;
		case CCamera_EnableVibrate:
			dllfile = L"Renderer2.dll";
			fullname = "pleione::CCamera::EnableVibrate(unsigned long, class esl::_vector<struct esl::__vector4<float>> const &)";
			dllfunction = "?EnableVibrate@CCamera@pleione@@QAEXKABV?$_vector@U?$__vector4@M@esl@@@esl@@@Z";
			break;
		case CRendererContext_RenderSkyTexture:
			dllfile = L"Renderer2.dll";
			fullname = "pleione::CRendererContext::_RenderSkyTexture";
			dllfunction = "?_RenderSkyTexture@CRendererContext@pleione@@AAEXXZ";
			break;

			// standard.dll

		case CDungeonRegion_SetFog:
			dllfile = L"standard.dll";
			fullname = "core::CDungeonRegion::SetFog";
			dllfunction = "?SetFog@CDungeonRegion@core@@QAEX_N@Z";
			break;
		case IParameterBase2_GetCombatPower:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetCombatPower";
			dllfunction = "?GetCombatPower@IParameterBase2@core@@QBEMXZ";
			break;
		case IItem_GetInterfaceDurability:
			dllfile = L"standard.dll";
			fullname = "core::IItem::GetInterfaceDurability";
			dllfunction = "?GetInterfaceDurability@IItem@core@@QBEKXZ";
			break;
		case IItem_GetInterfaceDurabilityMax:
			dllfile = L"standard.dll";
			fullname = "core::IItem::GetInterfaceDurabilityMax";
			dllfunction = "?GetInterfaceDurabilityMax@IItem@core@@QBEKXZ";
			break;
		case IItem_GetColor:
			dllfile = L"standard.dll";
			fullname = "core::IItem::GetColor";
			dllfunction = "?GetColor@IItem@core@@QBEKK@Z";
			break;
		case ICharacter_IsElf:
			dllfile = L"standard.dll";
			fullname = "core::ICharacter::IsElf";
			dllfunction = "?IsElf@ICharacter@core@@QBE_NXZ";
			break;
		case ICharacter_IsGiant:
			dllfile = L"standard.dll";
			fullname = "core::ICharacter::IsGiant";
			dllfunction = "?IsGiant@ICharacter@core@@QBE_NXZ";
			break;
		case ICharacter_IsPet:
			dllfile = L"standard.dll";
			fullname = "core::ICharacter::IsPet";
			dllfunction = "?IsPet@ICharacter@core@@QBE_NXZ";
			break;
		case ICharacter_IsNPC:
			dllfile = L"standard.dll";
			fullname = "core::ICharacter::IsNPC";
			dllfunction = "?IsNPC@ICharacter@core@@QBE_NXZ";
			break;
		case ICharacter_IsGoodNPC:
			dllfile = L"standard.dll";
			fullname = "core::ICharacter::IsGoodNPC";
			dllfunction = "?IsGoodNPC@ICharacter@core@@QBE_NXZ";
			break;
		case IParameterBase2_GetExploLevel:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetExploLevel";
			dllfunction = "?GetExploLevel@IParameterBase2@core@@QBEGXZ";
			break;
		case IParameter_GetExploExpPercent:
			dllfile = L"standard.dll";
			fullname = "core::IParameter::GetExploExpPercent";
			dllfunction = "?GetExploExpPercent@IParameter@core@@QBEMXZ";
			break;
		case ISkillMgr_IsEnableMovingAim:
			dllfile = L"standard.dll";
			fullname = "core::ISkillMgr::IsEnableMovingAim";
			dllfunction = "?IsEnableMovingAim@ISkillMgr@core@@QBE_NG@Z";
			break;
		case ISkillMgr_GetBasicRangedAttackSkill:
			dllfile = L"standard.dll";
			fullname = "core::ISkillMgr::GetBasicRangedAttackSkill";
			dllfunction = "?GetBasicRangedAttackSkill@ISkillMgr@core@@QBEGXZ";
			break;
		case IParameter_GetStrength:
			dllfile = L"standard.dll";
			fullname = "core::IParameter::GetStrength";
			dllfunction = "?GetStrength@IParameter@core@@QAEMXZ";
			break;
		case IParameter_GetDexterity:
			dllfile = L"standard.dll";
			fullname = "core::IParameter::GetDexterity";
			dllfunction = "?GetDexterity@IParameter@core@@QAEMXZ";
			break;
		case IParameter_GetIntelligence:
			dllfile = L"standard.dll";
			fullname = "core::IParameter::GetIntelligence";
			dllfunction = "?GetIntelligence@IParameter@core@@QAEMXZ";
			break;
		case IParameter_GetWill:
			dllfile = L"standard.dll";
			fullname = "core::IParameter::GetWill";
			dllfunction = "?GetWill@IParameter@core@@QAEMXZ";
			break;
		case IParameter_GetLuck:
			dllfile = L"standard.dll";
			fullname = "core::IParameter::GetLuck";
			dllfunction = "?GetLuck@IParameter@core@@QAEMXZ";
			break;
		case IParameterBase2_GetLife:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetLife";
			dllfunction = "?GetLife@IParameterBase2@core@@QAEMXZ";
			break;
		case IParameterBase2_GetLifeDamaged:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetLifeDamaged";
			dllfunction = "?GetLifeDamaged@IParameterBase2@core@@QAEMXZ";
			break;
		case IParameterBase2_GetLifeMax:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetLifeMax";
			dllfunction = "?GetLifeMax@IParameterBase2@core@@QAEMXZ";
			break;
		case IParameterBase2_GetLifeMaxBase:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetLifeMaxBase";
			dllfunction = "?GetLifeMaxBase@IParameterBase2@core@@QAEMXZ";
			break;
		case IParameterBase2_GetInterfaceLifeDamaged:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetInterfaceLifeDamaged";
			dllfunction = "?GetInterfaceLifeDamaged@IParameter@core@@UAEMXZ";
			break;
		case IParameterBase2_GetLifePercent:
			dllfile = L"standard.dll";
			fullname = "core::IParameterBase2::GetLifePercent";
			dllfunction = "?GetLifePercent@IParameter@core@@QAEMXZ";
			break;
		case IItem_IsPoisoned:
			dllfile = L"standard.dll";
			fullname = "core::IItem::IsPoisoned";
			dllfunction = "?IsPoisoned@IItem@core@@QBE_NXZ";
			break;
		case IItem_GetItemPotionPoison:
			dllfile = L"standard.dll";
			fullname = "core::IItem::GetItemPotionPoison";
			dllfunction = "?GetItemPotionPoison@IItem@core@@QBEGXZ";
			break;
		case IItem_GetItemBasePoison:
			dllfile = L"standard.dll";
			fullname = "core::IItem::GetItemBasePoison";
			dllfunction = "?GetItemBasePoison@IItem@core@@QBEGXZ";
			break;
		case IItem_GetItemPotionPoisonDurability:
			dllfile = L"standard.dll";
			fullname = "core::IItem::ItemPotionPoisonDurability";
			dllfunction = "?GetItemPotionPoisonDurability@IItem@core@@QBEKXZ";
			break;
		case Stdapi_FindItem:
			dllfile = L"standard.dll";
			fullname = "core::stdapi_FindItem";
			dllfunction = "?stdapi_FindItem@core@@YAPAVIItem@1@_K@Z";
			break;
		case CSetItemMgr_GetToolTipString:
			dllfile = L"standard.dll";
			fullname = "core::CSetItemMgr::GetToolTipString";
			dllfunction = "?GetToolTipString@CSetItemMgr@core@@QAE?AV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@_KJ_N@Z";
			break;
		case g_cStandardAPIMgrBlock:
			dllfile = L"standard.dll";
			fullname = "core::g_cStandardAPIMgrBlock";
			dllfunction = "?g_cStandardAPIMgrBlock@core@@3PAEA";
			break;
		case CStandardAPIMgr_CommonAPI:
			dllfile = L"standard.dll";
			fullname = "core::CStandardAPIMgr::CommonAPI";
			dllfunction = "?CommonAPI@CStandardAPIMgr@core@@QBEABVICommonAPI@2@XZ";
			break;
		case stdapi_ShowChattingMessage:
			dllfile = L"standard.dll";
			fullname = "core::stdapi_ShowChattingMessage";
			dllfunction = "?stdapi_ShowChattingMessage@core@@YAX_KABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@1KKE@Z";
			break;
		case stdapi_ShowCaption:
			dllfile = L"standard.dll";
			fullname = "core::stdapi_ShowCaption";
			dllfunction = "?stdapi_ShowCaption@core@@YAX_KABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@W4EMessageCaptionType@@KKK0@Z";
			break;
		case stdapi_GetGlobalTime:
			dllfile = L"standard.dll";
			fullname = "core::stdapi_GetGlobalTime";
			dllfunction = "?stdapi_GetGlobalTime@core@@YA_KXZ";
			break;
		case stdapi_GlobalTimeToGameTime:
			dllfile = L"standard.dll";
			fullname = "core::stdapi_GlobalTimeToGameTime";
			dllfunction = "?stdapi_GlobalTimeToGameTime@core@@YAX_KAAK11@Z";
			break;
		case IServiceMgr_IsUsingNaosSupport:
			dllfile = L"standard.dll";
			fullname = "core::IServiceMgr::IsUsingNaosSupport";
			dllfunction = "?IsUsingNaosSupport@IServiceMgr@core@@QBE_NXZ";
			break;
		case IServiceMgr_IsUsableContents:
			dllfile = L"standard.dll";
			fullname = "core::IServiceMgr::IsUsableContents";
			dllfunction = "?IsUsableContents@IServiceMgr@core@@QBE_NW4EServiceContents@2@@Z";
			break;
		case ISkillMgr_Complete:
			dllfile = L"standard.dll";
			fullname = "core::ISkillMgr::Complete";
			dllfunction = "?Complete@ISkillMgr@core@@UAE_NGVCMessage@mint@@@Z";
			break;
		case CConditionDescMgr_FindConditionID:
			dllfile = L"standard.dll";
			fullname = "core::CConditionDescMgr::FindConditionID";
			dllfunction = "?FindConditionID@CConditionDescMgr@core@@QBE?AW4ECharacterCondition@@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@@Z";
			break;
		case CConditionDescMgr_LoadData:
			dllfile = L"standard.dll";
			fullname = "core::CConditionDescMgr::LoadData";
			dllfunction = "?LoadData@CConditionDescMgr@core@@AAE_NABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@@Z";
			break;
		case CConditionDescMgr_LoadXML:
			dllfile = L"standard.dll";
			fullname = "core::CConditionDescMgr::LoadXML";
			dllfunction = "?LoadXML@CConditionDescMgr@core@@AAE_NABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@@Z";
			break;
		case IConditionMgr_GetConditionNum:
			dllfile = L"standard.dll";
			fullname = "core::IConditionMgr::GetConditionNum";
			dllfunction = "?GetConditionNum@IConditionMgr@core@@QBEKXZ";
			break;
		case IConditionMgr_WriteToDB:
			dllfile = L"standard.dll";
			fullname = "core::IConditionMgr::WriteToDB";
			dllfunction = "?WriteToDB@IConditionMgr@core@@UBE?BVCMessage@mint@@XZ";
			break;
		case IConditionMgr_WriteToFile:
			dllfile = L"standard.dll";
			fullname = "core::IConditionMgr::WriteToFile";
			dllfunction = "?WriteToFile@IConditionMgr@core@@UBEXAAVCFileBase@esl@@0_N@Z";
			break;
		case IConditionMgr_WriteMsgFast:
			dllfile = L"standard.dll";
			fullname = "core::IConditionMgr::WriteMsgFast";
			dllfunction = "?WriteMsgFast@IConditionMgr@core@@UBEXAAVCMessage@mint@@K@Z";
			break;
		case IConditionMgr_Update:
			dllfile = L"standard.dll";
			fullname = "core::IConditionMgr::Update";
			dllfunction = "?Update@IConditionMgr@core@@UAEXAAV?$vector@_KV?$allocator@_K@stlpx_std@@@stlpx_std@@AAV?$map@KV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@U?$less@K@stlpx_std@@V?$allocator@U?$pair@$$CBKV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@@stlpx_std@@@4@@4@@Z";
			break;

			// mint.dll
		case Mint_Write16:
			dllfile = L"Mint.dll";
			fullname = "mint::CMessage::WriteU16";
			dllfunction = "?WriteU16@CMessage@mint@@QAEAAV12@G@Z";
			break;
		case Mint_CMessage:
			dllfile = L"Mint.dll";
			fullname = "mint::CMessage::CMessage";
			dllfunction = "??0CMessage@mint@@QAE@K_K@Z";
			break;

			// skill.dll
		case CSkillDiceThrowing_GetSkillUsable:
			dllfile = L"skill.dll";
			fullname = "core::CSkillDiceThrowing::GetSkillUsable(struct core::SSkillContext &, unsigned __int64, enum  ECursorType *, float *)";
			dllfunction = "?GetSkillUsable@CSkillDiceThrowing@core@@UBE?AW4ESkillProcessType@2@AAUSSkillContext@2@_KPAW4ECursorType@@PAM@Z";
			break;

		default:
			return false;
		}

		HMODULE dllmodule = GetModuleHandle(dllfile);
		LPBYTE address = (LPBYTE)GetProcAddress(dllmodule, dllfunction);
		if (address == NULL) {
			log("Error: Could not find %s in %ls.\n", fullname, dllfile);
			DllAddresses[addrNumber] = NULL;
			return false;
		}
		log("Success: Found %s in %ls.\n", fullname, dllfile);
		DllAddresses[addrNumber] = address;
		return true;
	}
}
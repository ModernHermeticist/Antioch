using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlayerGuiController : MonoBehaviour 
{
	[SerializeField]
    private Stat health;
	[SerializeField]
	private Stat stamina;
	[SerializeField]
	private EssenceValue essenceValue;

	private PlayerStatController pSC;

	// Player Stats
	private float curHp;
	private float maxHp;
	private float curStam;
	private float maxStam;
	private int curEssence;

	void Awake () 
	{
		pSC = GetComponent<PlayerStatController>();
		Initialize();
		health.Initialize(curHp, maxHp);
		stamina.Initialize(curStam, maxStam);
		essenceValue.Initialize(curEssence);
	}
	

	void Update () 
	{
		if (Input.GetKeyDown(KeyCode.Q))
        {
            health.CurrentVal -= 10;
			pSC.HpValue -= 10;
        }
        if (Input.GetKeyDown(KeyCode.E))
        {
            health.CurrentVal += 10;
			pSC.HpValue += 10;
        }
		if(Input.GetKeyDown(KeyCode.F))
		{
			stamina.CurrentVal -= 5;
			pSC.StamValue -= 5;
		}
		if(Input.GetKeyDown(KeyCode.G))
		{
			stamina.CurrentVal += 5;
			pSC.StamValue += 5;
		}
        if (Input.GetKeyDown(KeyCode.T))
        {
            pSC.EssenceValue += 5;
            essenceValue.CurrentVal += 5;
        }
        if (Input.GetKeyDown(KeyCode.Y))
        {
            pSC.EssenceValue -= 5;
            essenceValue.CurrentVal -= 5;
        }
	}

	void Initialize()
	{
		curHp = pSC.HpValue;
		maxHp = pSC.MaxHpValue;
		curStam = pSC.StamValue;
		maxStam = pSC.MaxStamValue;
		curEssence = pSC.EssenceValue;
	}
}



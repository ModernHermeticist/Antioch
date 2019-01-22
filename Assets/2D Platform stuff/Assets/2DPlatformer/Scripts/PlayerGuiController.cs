using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlayerGuiController : MonoBehaviour 
{
	[SerializeField]
    private Stat health;
	[SerializeField]
	private Stat stamina;

	private PlayerStatController playerStatController;

	// Player Stats
	private float curHp;
	private float maxHp;
	private float curStam;
	private float maxStam;

	void Awake () 
	{
		playerStatController = GetComponent<PlayerStatController>();
		Initialize();
		health.Initialize(curHp, maxHp);
		stamina.Initialize(curStam, maxStam);
	}
	

	void Update () 
	{
		if (Input.GetKeyDown(KeyCode.Q))
        {
            health.CurrentVal -= 10;
        }
        if (Input.GetKeyDown(KeyCode.E))
        {
            health.CurrentVal += 10;
        }
		if(Input.GetKeyDown(KeyCode.F))
		{
			stamina.CurrentVal -= 5;
		}
		if(Input.GetKeyDown(KeyCode.G))
		{
			stamina.CurrentVal += 5;
		}
	}

	void Initialize()
	{
		curHp = playerStatController.getCurHp();
		maxHp = playerStatController.getMaxHp();
		curStam = playerStatController.getCurStam();
		maxStam = playerStatController.getMaxStam();
	}
}



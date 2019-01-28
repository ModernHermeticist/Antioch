using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class EssenceValue : MonoBehaviour
{
    [SerializeField]
    private Text valueText;
	[SerializeField]
    private int currentVal;

    public int CurrentVal
    {
        get
        {
            return currentVal;
        }

        set
        {
			currentVal = value;
            valueText.text = "" + value;
        }
    }

    void Update()
    {
        if ("" + currentVal != valueText.text)
		{
			valueText.text = "" + currentVal;
		}
    }

    public void Initialize(int initVal)
    {
        currentVal = initVal;
    }
}
